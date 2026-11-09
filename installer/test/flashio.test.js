import { test } from "node:test";
import assert from "node:assert/strict";
import { md5, toHex } from "../js/md5.js";
import {
  eraseRegionVerified, normalizeMd5Response, readFlashVerified, writeImagesVerified,
} from "../js/flashio.js";

const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

function makeFlash(size) {
  const flash = new Uint8Array(size);
  for (let i = 0; i < size; i++) flash[i] = (i * 31 + (i >> 8)) & 0xff;
  return flash;
}

// A simulated stub: answers READ_FLASH with frames like the real one. `mangle(session, frames)`
// may damage a session's frames (session is 0-based over all attempts).
function simulate(flash, { mangle, noDrain = false, commandResult = 0 } = {}) {
  const sim = { sessions: 0, acks: [], drains: 0, commands: [], reads: 0 };
  const queue = [];
  const transport = {
    serialErrors: 0,
    async read(timeout, signal) {
      sim.reads++;
      if (signal?.aborted) throw Object.assign(new Error("Canceled"), { name: "AbortError" });
      if (!queue.length) throw new Error("Serial data stream stopped: Possible serial noise or corruption.");
      const next = queue.shift();
      if (next instanceof Error) throw next;
      return next;
    },
    async write(bytes) { sim.acks.push(u32(bytes, 0)); },
    flushInput() { queue.length = 0; },
  };
  if (!noDrain) transport.drainInput = async () => { sim.drains++; queue.length = 0; };
  const loader = {
    ESP_READ_FLASH: 0xd2,
    transport,
    async checkCommand(desc, op, pkt) {
      sim.commands.push({ op, addr: u32(pkt, 0), len: u32(pkt, 4), packet: u32(pkt, 8), inFlight: u32(pkt, 12) });
      const { addr, len } = sim.commands.at(-1);
      let frames = [];
      for (let o = 0; o < len; o += 0x1000) frames.push(flash.slice(addr + o, addr + Math.min(o + 0x1000, len)));
      frames.push(md5(flash.subarray(addr, addr + len)));
      if (mangle) frames = mangle(sim.sessions, frames) ?? frames;
      sim.sessions++;
      queue.length = 0;
      queue.push(...frames);
      return commandResult;
    },
  };
  return { sim, loader, transport, queue };
}

test("readFlashVerified reads clean data in chunks with the READ_FLASH protocol", async () => {
  const flash = makeFlash(0x25000 + 0x123);
  const { sim, loader } = simulate(flash);
  const progress = [];
  const out = await readFlashVerified(loader, 0, flash.length, { onProgress: (d, t) => progress.push([d, t]) });
  assert.deepEqual(out, flash);
  assert.equal(sim.commands.length, 3);
  assert.deepEqual(sim.commands[0], { op: 0xd2, addr: 0, len: 0x10000, packet: 0x1000, inFlight: 16 });
  assert.equal(sim.commands[2].addr, 0x20000);
  assert.equal(sim.acks.at(0), 0x1000); // running total of bytes received
  assert.equal(sim.acks.at(15), 0x10000);
  assert.deepEqual(progress.at(-1), [flash.length, flash.length]);
  assert.equal(progress.length, 3);
});

test("readFlashVerified honors addr offset and small sizes", async () => {
  const flash = makeFlash(0x4000);
  const { loader } = simulate(flash);
  assert.deepEqual(await readFlashVerified(loader, 0x1000, 100), flash.subarray(0x1000, 0x1064));
  assert.deepEqual(await readFlashVerified(loader, 0, 0), new Uint8Array(0));
});

test("dropped bytes in the middle of a transfer are retried", async () => {
  const flash = makeFlash(0x20000);
  const logs = [];
  const { sim, loader } = simulate(flash, {
    mangle: (n, frames) => { if (n === 0) frames[3] = frames[3].subarray(0, 4000); },
  });
  const out = await readFlashVerified(loader, 0, flash.length, { onLog: (m) => logs.push(m) });
  assert.deepEqual(out, flash);
  assert.equal(sim.commands.length, 3); // chunk 0 twice, chunk 1 once
  assert.equal(sim.drains, 1);
  assert.ok(logs.some((m) => /expected 4096 bytes but received 4000/.test(m)));
  assert.equal(sim.acks.includes(0x10000), true); // recovery acknowledged the full length
});

test("dropped bytes in the last frame are retried", async () => {
  const flash = makeFlash(0x10000);
  const { loader } = simulate(flash, {
    mangle: (n, frames) => { if (n === 0) frames[15] = frames[15].subarray(0, 4000); },
  });
  assert.deepEqual(await readFlashVerified(loader, 0, flash.length), flash);
});

test("a digest mismatch is retried", async () => {
  const flash = makeFlash(0x10000);
  const logs = [];
  const { loader } = simulate(flash, {
    mangle: (n, frames) => { if (n < 2) { frames[2] = frames[2].slice(); frames[2][7] ^= 0xff; } },
  });
  assert.deepEqual(await readFlashVerified(loader, 0, flash.length, { onLog: (m) => logs.push(m) }), flash);
  assert.equal(logs.filter((m) => /digest mismatch/.test(m)).length, 2);
});

test("retries exhausted throws and never returns bad data", async () => {
  const flash = makeFlash(0x10000);
  const logs = [];
  const { sim, loader } = simulate(flash, {
    mangle: (n, frames) => { frames[0] = frames[0].slice(); frames[0][0] ^= 1; },
  });
  await assert.rejects(readFlashVerified(loader, 0, flash.length, { retries: 2, onLog: (m) => logs.push(m) }),
    /failed after 3 attempts/);
  assert.equal(sim.commands.length, 3);
  assert.match(logs.at(-1), /attempts=3 retries=2/);
});

test("timeouts, bad command status, wrong digest length and oversized frames are retried", async () => {
  const flash = makeFlash(0x2000);
  const faults = [
    (f) => { f.length = 1; },                                  // stream stops after one frame
    (f) => { f[f.length - 1] = new Uint8Array(15); },            // digest of the wrong length
    (f) => { f[0] = new Uint8Array(0x3000); },                  // frame larger than the chunk
    (f) => { f.splice(0, 0, ...Array(101).fill(new Uint8Array(0))); }, // too many empty frames
    (f) => { f.splice(1, 0, new Uint8Array(0)); },               // a few empty frames are tolerated
  ];
  for (const fault of faults) {
    const { loader } = simulate(flash, { mangle: (n, frames) => { if (n === 0) fault(frames); } });
    assert.deepEqual(await readFlashVerified(loader, 0, flash.length), flash);
  }
  const bad = simulate(flash, { commandResult: 5 });
  await assert.rejects(readFlashVerified(bad.loader, 0, flash.length, { retries: 1 }), /failed after 2 attempts/);
});

test("an increase in serial errors makes the attempt fail even if the digest matched", async () => {
  const flash = makeFlash(0x2000);
  const logs = [];
  const { loader, transport } = simulate(flash, {
    mangle: (n) => { if (n === 0) transport.serialErrors++; },
  });
  assert.deepEqual(await readFlashVerified(loader, 0, flash.length, { onLog: (m) => logs.push(m) }), flash);
  assert.ok(logs.some((m) => /serial errors increased by 1/.test(m)));
});

test("recovery without drainInput falls back to a pause and flush; logging errors are ignored", async () => {
  const flash = makeFlash(0x2000);
  const { loader } = simulate(flash, {
    noDrain: true,
    mangle: (n, frames) => { if (n === 0) frames[0] = frames[0].subarray(0, 10); },
  });
  const out = await readFlashVerified(loader, 0, flash.length, { onLog: () => { throw new Error("ignored"); } });
  assert.deepEqual(out, flash);
});

test("recovery tolerates a failing acknowledgment write", async () => {
  const flash = makeFlash(0x2000);
  const { loader, transport } = simulate(flash, {
    mangle: (n, frames) => { if (n === 0) { frames[0] = frames[0].subarray(0, 10); transport.write = async () => { throw new Error("gone"); }; } },
  });
  await assert.rejects(readFlashVerified(loader, 0, flash.length, { retries: 0 }), /failed after 1 attempts/);
});

test("abort stops reading promptly", async () => {
  const flash = makeFlash(0x30000);
  const ac = new AbortController();
  const { sim, loader } = simulate(flash);
  const progress = [];
  await assert.rejects(
    readFlashVerified(loader, 0, flash.length, {
      signal: ac.signal,
      onProgress: (d) => { progress.push(d); ac.abort(); },
    }),
    { name: "AbortError" });
  assert.equal(progress.length, 1);
  assert.equal(sim.commands.length, 1);

  const pre = new AbortController(); pre.abort();
  await assert.rejects(readFlashVerified(simulate(flash).loader, 0, 100, { signal: pre.signal }), { name: "AbortError" });
});

test("abort during a transfer is not retried", async () => {
  const flash = makeFlash(0x10000);
  const ac = new AbortController();
  const { sim, loader, transport } = simulate(flash);
  const realRead = transport.read;
  let n = 0;
  transport.read = async (...a) => { if (++n === 3) ac.abort(); return realRead(...a); };
  await assert.rejects(readFlashVerified(loader, 0, flash.length, { signal: ac.signal }), { name: "AbortError" });
  assert.equal(sim.commands.length, 1);
});

// ---------------------------------------------------------------------- writes

function writerLoader({ md5Impl, writeImpl } = {}) {
  const calls = { writeFlash: [], md5: [], drains: 0 };
  const flash = new Map();
  const loader = {
    transport: { drainInput: async () => { calls.drains++; } },
    async writeFlash(options) {
      calls.writeFlash.push(options);
      if (writeImpl) await writeImpl(options);
      for (const { data, address } of options.fileArray) {
        const padded = data.length % 4 ? Uint8Array.from([...data, ...Array(4 - (data.length % 4)).fill(0xff)]) : data;
        flash.set(address, padded);
        options.reportProgress?.(0, 0, padded.length);
        options.reportProgress?.(0, padded.length / 2, padded.length);
        options.reportProgress?.(0, padded.length, padded.length);
        assert.equal(options.calculateMD5Hash(padded), toHex(md5(padded)));
      }
    },
    async flashMd5sum(addr, size) {
      calls.md5.push([addr, size]);
      if (md5Impl) return md5Impl(addr, size, calls.md5.length);
      return toHex(md5(flash.get(addr).subarray(0, size)));
    },
  };
  return { loader, calls, flash };
}

test("writeImagesVerified passes the required esptool-js options and verifies each image", async () => {
  const { loader, calls } = writerLoader();
  const progress = [];
  await writeImagesVerified(loader, [
    { address: 0, data: new Uint8Array(1000).fill(1) },
    { address: 0x8000, data: new Uint8Array(3000).fill(2) },
  ], { onProgress: (d, t, l) => progress.push([d, t, l]) });
  assert.equal(calls.writeFlash.length, 2);
  const o = calls.writeFlash[1];
  assert.equal(o.flashMode, "keep");
  assert.equal(o.flashFreq, "keep");
  assert.equal(o.flashSize, "keep");
  assert.equal(o.compress, true);
  assert.equal(o.eraseAll, false);
  assert.equal(o.fileArray[0].address, 0x8000);
  assert.deepEqual(calls.md5, [[0, 1000], [0x8000, 3000]]);
  assert.deepEqual(progress.at(-1), [4000, 4000, "Writing at 0x8000"]);
  assert.ok(progress.some(([d, t, l]) => d === 2500 && t === 4000 && l === "Writing at 0x8000"));
});

test("writeImagesVerified verifies the padded length for odd-sized images", async () => {
  const { loader, calls } = writerLoader();
  await writeImagesVerified(loader, [{ address: 0x10000, data: new Uint8Array(1001).fill(3) }]);
  assert.deepEqual(calls.md5, [[0x10000, 1004]]);
});

test("a wrong MD5 is retried 3 times and then fails", async () => {
  const { loader, calls } = writerLoader({ md5Impl: () => "0".repeat(32) });
  const logs = [];
  await assert.rejects(
    writeImagesVerified(loader, [{ address: 0, data: new Uint8Array(8) }], { onLog: (m) => logs.push(m) }),
    /Verification failed at 0x0/);
  assert.equal(calls.md5.length, 3);
  assert.equal(calls.drains, 2);
  assert.equal(logs.length, 3);
});

test("md5sum command errors are retried and can recover; ROM-style responses are understood", async () => {
  let real;
  const { loader, calls } = writerLoader({
    md5Impl: (addr, size, n) => {
      if (n < 3) throw new Error("timeout");
      return real(addr, size);
    },
  });
  real = (addr, size) => toHex(md5(loader.__flash.get(addr).subarray(0, size)));
  loader.__flash = new Map();
  const origWrite = loader.writeFlash;
  loader.writeFlash = async (o) => { loader.__flash.set(o.fileArray[0].address, o.fileArray[0].data); return origWrite(o); };
  await writeImagesVerified(loader, [{ address: 4, data: new Uint8Array(16).fill(9) }]);
  assert.equal(calls.md5.length, 3);

  const data = new Uint8Array(16).fill(5);
  const hex = toHex(md5(data));
  const ascii = toHex(new TextEncoder().encode(hex)).toUpperCase(); // ROM: 32 ASCII characters
  const rom = writerLoader({ md5Impl: () => ascii });
  await writeImagesVerified(rom.loader, [{ address: 0, data }]);
  assert.equal(normalizeMd5Response(ascii), hex);
  assert.equal(normalizeMd5Response(hex.toUpperCase()), hex);
});

test("writeImagesVerified without drainInput still retries", async () => {
  const { loader, calls } = writerLoader({ md5Impl: () => "bad" });
  delete loader.transport;
  await assert.rejects(writeImagesVerified(loader, [{ address: 0, data: new Uint8Array(4) }], { attempts: 2 }), /Verification failed/);
  assert.equal(calls.md5.length, 2);
});

test("writeImagesVerified skips empty images and propagates writeFlash errors", async () => {
  const { loader, calls } = writerLoader();
  await writeImagesVerified(loader, [{ address: 0, data: new Uint8Array(0) }]);
  assert.equal(calls.writeFlash.length, 0);
  const failing = writerLoader({ writeImpl: async () => { throw new Error("MD5 of file does not match data in flash!"); } });
  await assert.rejects(writeImagesVerified(failing.loader, [{ address: 0, data: new Uint8Array(4) }]), /MD5 of file/);
});

test("writeImagesVerified can be aborted between images and during progress", async () => {
  const ac = new AbortController();
  const { loader, calls } = writerLoader();
  await assert.rejects(writeImagesVerified(loader, [
    { address: 0, data: new Uint8Array(8) }, { address: 0x100, data: new Uint8Array(8) },
  ], { signal: ac.signal, onProgress: () => ac.abort() }), { name: "AbortError" });
  assert.equal(calls.writeFlash.length, 1);

  const ac2 = new AbortController();
  const w = writerLoader();
  ac2.abort();
  await assert.rejects(writeImagesVerified(w.loader, [{ address: 0, data: new Uint8Array(8) }], { signal: ac2.signal }), { name: "AbortError" });
  assert.equal(w.calls.writeFlash.length, 0);

  // Aborting from inside the progress callback of writeFlash stops the write itself.
  const ac3 = new AbortController();
  const w3 = writerLoader({ writeImpl: async (o) => { ac3.abort(); o.reportProgress(0, 1, 2); } });
  await assert.rejects(writeImagesVerified(w3.loader, [{ address: 0, data: new Uint8Array(8) }], { signal: ac3.signal }), { name: "AbortError" });
  assert.equal(w3.calls.md5.length, 0);
});

test("a verification abort stops the md5 retry loop", async () => {
  const ac = new AbortController();
  const { loader, calls } = writerLoader({ md5Impl: () => { ac.abort(); return "bad"; } });
  await assert.rejects(writeImagesVerified(loader, [{ address: 0, data: new Uint8Array(4) }], { signal: ac.signal }), { name: "AbortError" });
  assert.equal(calls.md5.length, 1);
});

test("eraseRegionVerified writes 0xFF over the region", async () => {
  const { loader, calls } = writerLoader();
  await eraseRegionVerified(loader, 0x9000, 0x6000);
  const f = calls.writeFlash[0].fileArray[0];
  assert.equal(f.address, 0x9000);
  assert.equal(f.data.length, 0x6000);
  assert.ok(f.data.every((b) => b === 0xff));
});
