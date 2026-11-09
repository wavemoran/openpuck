// Verified flash reading and writing on top of an esptool-js ESPLoader running the stub.
import { md5, toHex } from "./md5.js";
import { MD5_VERIFY_ATTEMPTS } from "./constants.js";
import { sleep, throwIfAborted } from "./transport.js";

const u32le = (n) => new Uint8Array([n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff]);
const hx = (n) => "0x" + (n >>> 0).toString(16);
const PACKET = 0x1000;

/**
 * Read flash with a per-chunk MD5 check and retries. Never writes.
 * Uses the stub's READ_FLASH command (0xD2) the way Python esptool does.
 * @param {object} loader esptool-js ESPLoader (stub running) with a RobustTransport
 * @param {number} addr
 * @param {number} size
 * @param {{chunkSize?: number, maxInFlight?: number, retries?: number, packetTimeout?: number,
 *   onProgress?: (done: number, total: number) => void, onLog?: (msg: string) => void,
 *   signal?: AbortSignal}} [options]
 * @returns {Promise<Uint8Array>}
 */
export async function readFlashVerified(loader, addr, size, options = {}) {
  const { chunkSize = 0x10000, maxInFlight = 16, retries = 10, packetTimeout = 5000,
    onProgress, onLog, signal } = options;
  const transport = loader.transport;
  const log = (m) => { try { onLog?.(m); } catch (_) { /* ignore */ } };
  const out = new Uint8Array(size);

  // One attempt at one chunk; returns null on success or a failure description.
  async function attempt(caddr, len, dest) {
    const errorsBefore = transport.serialErrors ?? 0;
    let received = 0;
    let failure = null;
    try {
      const pkt = new Uint8Array(16);
      pkt.set(u32le(caddr), 0); pkt.set(u32le(len), 4);
      pkt.set(u32le(PACKET), 8); pkt.set(u32le(maxInFlight), 12);
      const res = await loader.checkCommand("read flash", loader.ESP_READ_FLASH, pkt);
      if (res != 0) throw new Error("Failed to read memory: " + res);

      let empties = 0;
      while (received < len) {
        const p = await transport.read(packetTimeout, signal);
        if (p.length === 0) { // stray delimiter, not a data frame
          if (++empties > 100) throw new Error("too many empty frames");
          continue;
        }
        if (received + p.length > len) {
          throw new Error(`corrupt data: packet of ${p.length} bytes at offset ${received} exceeds chunk length ${len}`);
        }
        if (received + p.length < len && p.length < PACKET) {
          throw new Error(`corrupt data: expected ${PACKET} bytes but received ${p.length} at offset ${received}`);
        }
        dest.set(p, received);
        received += p.length;
        await transport.write(u32le(received)); // acknowledge, SLIP-encoded by write()
      }

      const digest = await transport.read(packetTimeout, signal);
      if (digest.length !== 16) {
        throw new Error(`expected 16-byte digest, got ${digest.length} bytes`);
      }
      const mine = md5(dest.subarray(0, len));
      if (toHex(mine) !== toHex(digest)) {
        throw new Error(`digest mismatch: device ${toHex(digest)}, host ${toHex(mine)}`);
      }
    } catch (e) {
      if (signal && signal.aborted) throw e;
      failure = `${e && e.message ? e.message : e} (received ${received}/${len})`;
    }
    const delta = (transport.serialErrors ?? 0) - errorsBefore;
    if (delta > 0) failure = (failure ? failure + "; " : "") + `serial errors increased by ${delta}`;
    return { failure, received };
  }

  // Let the stub finish (ack the full length), then drop whatever is still in flight.
  async function recover(len, midTransfer) {
    if (midTransfer) {
      try { await transport.write(u32le(len)); } catch (_) { /* ignore */ }
    }
    if (typeof transport.drainInput === "function") await transport.drainInput(500, 4000);
    else { await sleep(300); transport.flushInput?.(); }
  }

  let attempts = 0;
  let retried = 0;
  for (let done = 0; done < size; done += chunkSize) {
    const caddr = addr + done;
    const len = Math.min(chunkSize, size - done);
    const dest = out.subarray(done, done + len);
    let ok = false;
    for (let n = 0; n <= retries && !ok; n++) {
      throwIfAborted(signal);
      attempts++;
      const { failure, received } = await attempt(caddr, len, dest);
      if (!failure) { ok = true; break; }
      log(`chunk ${hx(caddr)} attempt ${n + 1}/${retries + 1} failed: ${failure}`);
      if (n < retries) {
        retried++;
        await recover(len, received < len);
      }
    }
    if (!ok) {
      log(`read summary: attempts=${attempts} retries=${retried}`);
      throw new Error(`Reading flash at ${hx(caddr)} failed after ${retries + 1} attempts`);
    }
    onProgress?.(done + len, size);
  }
  log(`read summary: attempts=${attempts} retries=${retried}`);
  return out;
}

/**
 * Normalize the MD5 returned by loader.flashMd5sum: the stub returns 16 raw bytes
 * (32 hex chars); the ROM returns 32 ASCII hex characters (64 hex chars).
 * @param {string} res
 * @returns {string} 32 lowercase hex characters
 */
export function normalizeMd5Response(res) {
  const s = String(res).toLowerCase();
  if (s.length === 64) {
    let ascii = "";
    for (let i = 0; i < 64; i += 2) ascii += String.fromCharCode(parseInt(s.slice(i, i + 2), 16));
    return ascii;
  }
  return s;
}

// esptool-js pads images to a multiple of 4 bytes with 0xFF before writing.
function padTo4(data) {
  if (data.length % 4 === 0) return data;
  const out = new Uint8Array(data.length + (4 - (data.length % 4))).fill(0xff);
  out.set(data);
  return out;
}

/**
 * Write images, then verify each independently by asking the chip for the MD5
 * of the written region (retrying the md5sum command a few times).
 * @param {object} loader esptool-js ESPLoader (stub running)
 * @param {{address: number, data: Uint8Array}[]} images
 * @param {{onProgress?: (done: number, total: number, label: string) => void,
 *   onLog?: (msg: string) => void, signal?: AbortSignal, attempts?: number}} [options]
 *   Progress counts uncompressed bytes of the images.
 * @returns {Promise<void>}
 */
export async function writeImagesVerified(loader, images, options = {}) {
  const { onProgress, onLog, signal, attempts = MD5_VERIFY_ATTEMPTS } = options;
  const total = images.reduce((n, im) => n + im.data.length, 0);
  let base = 0;
  for (const im of images) {
    throwIfAborted(signal);
    const label = `Writing at ${hx(im.address)}`;
    const size = im.data.length;
    if (size === 0) continue;
    await loader.writeFlash({
      fileArray: [{ data: im.data, address: im.address }],
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      compress: true,
      eraseAll: false,
      reportProgress: (_index, written, compressedTotal) => {
        throwIfAborted(signal);
        const fraction = compressedTotal > 0 ? written / compressedTotal : 1;
        onProgress?.(base + Math.floor(size * fraction), total, label);
      },
      calculateMD5Hash: (image) => toHex(md5(image)),
    });
    onProgress?.(base + size, total, label);

    const padded = padTo4(im.data);
    const expected = toHex(md5(padded));
    let lastProblem = "";
    let verified = false;
    for (let n = 0; n < attempts && !verified; n++) {
      throwIfAborted(signal);
      try {
        const got = normalizeMd5Response(await loader.flashMd5sum(im.address, padded.length));
        if (got === expected) verified = true;
        else lastProblem = `device ${got}, expected ${expected}`;
      } catch (e) {
        lastProblem = e && e.message ? e.message : String(e);
      }
      if (!verified) {
        onLog?.(`verify at ${hx(im.address)} attempt ${n + 1}/${attempts} failed: ${lastProblem}`);
        if (n + 1 < attempts && typeof loader.transport?.drainInput === "function") {
          await loader.transport.drainInput();
        }
      }
    }
    if (!verified) {
      throw new Error(`Verification failed at ${hx(im.address)} (${lastProblem})`);
    }
    base += size;
  }
}

/**
 * Overwrite a region with 0xFF (an erase that is verified like any write).
 * @param {object} loader
 * @param {number} address
 * @param {number} size
 * @param {object} [options] same as writeImagesVerified
 * @returns {Promise<void>}
 */
export function eraseRegionVerified(loader, address, size, options) {
  return writeImagesVerified(loader, [{ address, data: new Uint8Array(size).fill(0xff) }], options);
}
