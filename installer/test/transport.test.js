import { test } from "node:test";
import assert from "node:assert/strict";
import { RobustTransport, abortError, sleep, throwIfAborted, withTimeout } from "../js/transport.js";

function fakeDevice() {
  const dev = { writes: [] };
  dev.newStream = () => {
    const old = dev.ctrl;
    dev.readable = new ReadableStream({ start(c) { dev.ctrl = c; } });
    return old;
  };
  dev.newStream();
  dev.writable = new WritableStream({ write(chunk) { dev.writes.push(chunk); } });
  return dev;
}

const frame = (t, bytes) => t.slipWriter(Uint8Array.from(bytes));
function randomBytesWithSpecials(n) {
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i++) {
    const r = Math.random();
    out[i] = r < 0.15 ? 0xc0 : r < 0.3 ? 0xdb : Math.floor(Math.random() * 256);
  }
  return out;
}

test("read decodes SLIP frames with escapes split at random chunk boundaries", async () => {
  const dev = fakeDevice();
  const t = new RobustTransport(dev);
  const loop = t.readLoop();
  const payloads = [];
  for (let i = 0; i < 25; i++) payloads.push(randomBytesWithSpecials(Math.floor(Math.random() * 600)));
  const wire = [];
  for (const p of payloads) wire.push(...t.slipWriter(p));
  // Deliver in random-sized chunks while reading concurrently.
  const feeder = (async () => {
    for (let i = 0; i < wire.length;) {
      const n = 1 + Math.floor(Math.random() * 300);
      dev.ctrl.enqueue(Uint8Array.from(wire.slice(i, i + n)));
      i += n;
      await sleep(0);
    }
  })();
  for (const p of payloads) {
    const got = await t.read(2000);
    assert.deepEqual(Array.from(got), Array.from(p));
  }
  await feeder;
  dev.ctrl.close();
  await loop;
});

test("read returns frames that arrive together and keeps the remainder", async () => {
  const t = new RobustTransport(fakeDevice());
  t.buffer = Uint8Array.from([...frame(t, [1, 2, 3]), ...frame(t, [4, 5]), ...frame(t, [])]);
  assert.deepEqual(Array.from(await t.read(100)), [1, 2, 3]);
  assert.deepEqual(Array.from(await t.read(100)), [4, 5]);
  assert.deepEqual(Array.from(await t.read(100)), []);
});

test("read grows its packet buffer for large frames", async () => {
  const t = new RobustTransport(fakeDevice());
  const big = new Uint8Array(0x3000).map((_, i) => i & 0xff);
  t.buffer = Uint8Array.from(t.slipWriter(big));
  assert.deepEqual(await t.read(100), big);
});

test("read fails on noise, bad escapes and silence", async () => {
  const t = new RobustTransport(fakeDevice());
  t.buffer = Uint8Array.from([0x41, 0x42]);
  await assert.rejects(t.read(50), /Invalid head of packet \(0x41\)/);
  t.buffer = Uint8Array.from([0xc0, 0xdb, 0x01]);
  await assert.rejects(t.read(50), /Invalid SLIP escape/);
  await assert.rejects(t.read(20), /Serial data stream stopped/);
  t.buffer = Uint8Array.from([0xc0, 1, 2]);
  await assert.rejects(t.read(20), /No serial data received/);
});

test("read can be canceled before and while waiting", async () => {
  const t = new RobustTransport(fakeDevice());
  const pre = new AbortController(); pre.abort();
  await assert.rejects(t.read(1000, pre.signal), { name: "AbortError" });
  const ac = new AbortController();
  setTimeout(() => ac.abort(), 20);
  const started = Date.now();
  await assert.rejects(t.read(10000, ac.signal), { name: "AbortError" });
  assert.ok(Date.now() - started < 1000);
});

test("recoverable serial errors are counted and reading continues on the new stream", async () => {
  const dev = fakeDevice();
  const t = new RobustTransport(dev);
  const seen = [];
  t.onSerialError = (name) => { seen.push(name); throw new Error("callback errors are ignored"); };
  const loop = t.readLoop();
  dev.ctrl.enqueue(Uint8Array.from(frame(t, [7])));
  assert.deepEqual(Array.from(await t.read(500)), [7]);
  const err = new Error("break"); err.name = "BreakError";
  dev.newStream().error(err);
  await sleep(10);
  dev.ctrl.enqueue(Uint8Array.from(frame(t, [8])));
  assert.deepEqual(Array.from(await t.read(500)), [8]);
  assert.equal(t.serialErrors, 1);
  assert.deepEqual(seen, ["BreakError"]);
  dev.ctrl.close();
  await loop;
});

test("fatal read errors end the loop; a DOMException reports device loss", async () => {
  let dev = fakeDevice();
  let t = new RobustTransport(dev);
  let loop = t.readLoop();
  const fatal = new Error("boom"); fatal.name = "NetworkError";
  dev.ctrl.error(fatal);
  await loop;
  assert.equal(t.serialErrors, 1);

  dev = fakeDevice();
  t = new RobustTransport(dev);
  let lost = 0;
  t.setDeviceLostCallback(() => { lost++; });
  loop = t.readLoop();
  dev.ctrl.error(new DOMException("gone", "NetworkError"));
  await loop;
  assert.equal(lost, 1);

  dev = fakeDevice();
  t = new RobustTransport(dev);
  loop = t.readLoop();
  dev.ctrl.error(new DOMException("gone", "NetworkError"));
  await loop; // no callback registered: just ends

  dev = fakeDevice();
  t = new RobustTransport(dev);
  loop = t.readLoop();
  dev.ctrl.error("not an Error");
  await loop;
  assert.equal(t.serialErrors, 1);
});

test("write SLIP-encodes to the device", async () => {
  const dev = fakeDevice();
  const t = new RobustTransport(dev);
  await t.write(Uint8Array.from([1, 0xc0, 0xdb]));
  assert.deepEqual(Array.from(dev.writes[0]), [0xc0, 1, 0xdb, 0xdc, 0xdb, 0xdd, 0xc0]);
});

test("helpers: abortError, throwIfAborted, sleep, withTimeout", async () => {
  assert.equal(abortError().name, "AbortError");
  assert.doesNotThrow(() => throwIfAborted(undefined));
  const ac = new AbortController();
  assert.doesNotThrow(() => throwIfAborted(ac.signal));
  await sleep(1, ac.signal);
  setTimeout(() => ac.abort(), 10);
  await assert.rejects(sleep(5000, ac.signal), { name: "AbortError" });
  await assert.rejects(sleep(5000, ac.signal), { name: "AbortError" });
  assert.throws(() => throwIfAborted(ac.signal), { name: "AbortError" });
  assert.equal(await withTimeout(Promise.resolve(5), 100), 5);
  await assert.rejects(withTimeout(new Promise(() => {}), 10, "thing"), /thing timed out after 10 ms/);
});
