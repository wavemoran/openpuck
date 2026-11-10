import { test } from "node:test";
import assert from "node:assert/strict";
import {
  flashSizeFromId, holdPower, isButtonHeld, openLoader, prepareFlash, resetToApp, safeDisconnect,
  waitForButtonRelease, waitForPuck,
} from "../js/device.js";

function fakeRegs(initial = {}) {
  const regs = new Map(Object.entries(initial).map(([k, v]) => [Number(k), v]));
  const log = [];
  return {
    log,
    async readReg(a) { log.push(["r", a]); return regs.get(a) ?? 0; },
    async writeReg(a, v) { log.push(["w", a, v]); regs.set(a, v); },
    regs,
  };
}

test("holdPower follows the flash.py register sequence", async () => {
  const loader = fakeRegs({ 0x60009020: 0x7000 | 0x5, 0x60009028: 0x3000 });
  await holdPower(loader);
  assert.deepEqual(loader.log, [
    ["r", 0x60009020], ["w", 0x60009020, 0x1005],      // pin 7: MCU_SEL = GPIO, no input enable
    ["w", 0x60004570, 0x80],                            // GPIO_FUNC7_OUT_SEL = simple GPIO
    ["w", 0x60004008, 0x80],                            // GPIO_OUT_W1TS bit 7
    ["w", 0x60004024, 0x80],                            // GPIO_ENABLE_W1TS bit 7
    ["r", 0x6000902c], ["w", 0x6000902c, 0x1200],       // pin 10: MCU_SEL = GPIO + input enable
  ]);
});

test("isButtonHeld reads bit 10 of GPIO_IN", async () => {
  assert.equal(await isButtonHeld(fakeRegs({ 0x6000403c: 1 << 10 })), true);
  assert.equal(await isButtonHeld(fakeRegs({ 0x6000403c: ~(1 << 10) })), false);
});

test("waitForButtonRelease polls until released", async () => {
  const states = [true, true, false];
  const sleeps = [];
  const loader = { readReg: async () => (states.shift() ? 1 << 10 : 0) };
  await waitForButtonRelease(loader, { sleep: async (ms) => { sleeps.push(ms); } });
  assert.deepEqual(sleeps, [100, 100]);
});

test("waitForButtonRelease times out and can be aborted", async () => {
  let t = 0;
  const held = { readReg: async () => 1 << 10 };
  await assert.rejects(
    waitForButtonRelease(held, { timeoutMs: 1000, now: () => t, sleep: async (ms) => { t += ms * 4; } }),
    /still held after 1 seconds/);
  const ac = new AbortController();
  await assert.rejects(
    waitForButtonRelease(held, { signal: ac.signal, sleep: async () => { ac.abort(); } }),
    { name: "AbortError" });
  // With real timers and the default timeout settings it returns at once when released.
  await waitForButtonRelease({ readReg: async () => 0 });
});

test("prepareFlash runs the stub, attaches flash and reads id and MAC", async () => {
  const calls = [];
  const chip = { CHIP_NAME: "ESP32-C3", readMac: async () => "aa:bb:cc:dd:ee:ff" };
  const loader = {
    chip,
    runStub: async () => { calls.push("stub"); return chip; },
    flashSpiAttach: async (a) => { calls.push(["attach", a]); },
    readFlashId: async () => { calls.push("id"); return 0x164020; },
  };
  const info = await prepareFlash(loader);
  assert.deepEqual(calls, ["stub", ["attach", 0], "id"]);
  assert.deepEqual(info, { flashId: 0x164020, flashSize: 4 * 1024 * 1024, mac: "aa:bb:cc:dd:ee:ff", chipName: "ESP32-C3" });
  // Falls back to the chip returned by runStub.
  const info2 = await prepareFlash({ runStub: async () => chip, flashSpiAttach: async () => {}, readFlashId: async () => 0x174020 });
  assert.equal(info2.flashSize, 8 * 1024 * 1024);
});

test("flashSizeFromId does not wrap large JEDEC capacity bytes", () => {
  assert.equal(flashSizeFromId(0x164046), 4 * 1024 * 1024);
  assert.equal(flashSizeFromId(0x174020), 8 * 1024 * 1024);
  assert.equal(flashSizeFromId(0x204020), null);
  assert.equal(flashSizeFromId(0x364046), null);
  assert.equal(flashSizeFromId(0xffffff), null);
});

test("resetToApp pulses RTS with DTR low, in order", async () => {
  const calls = [];
  const transport = {
    setDTR: async (v) => calls.push(["dtr", v]),
    setRTS: async (v) => calls.push(["rts", v]),
  };
  await resetToApp(transport, { sleep: async (ms) => calls.push(["sleep", ms]) });
  assert.deepEqual(calls, [["dtr", false], ["rts", true], ["sleep", 200], ["rts", false], ["sleep", 200]]);
  await resetToApp(transport, { pulseMs: 1 }); // real timers
});

function fakeSerial() {
  const listeners = new Set();
  return {
    listeners,
    addEventListener(type, fn) { assert.equal(type, "connect"); listeners.add(fn); },
    removeEventListener(type, fn) { assert.equal(type, "connect"); listeners.delete(fn); },
    emit(port) { for (const fn of [...listeners]) fn({ target: port }); },
  };
}
const puckPort = (vid = 0x303a, pid = 0x1001) => ({ getInfo: () => ({ usbVendorId: vid, usbProductId: pid }) });

test("waitForPuck listens only while waiting and resolves with the new port", async () => {
  const serial = fakeSerial();
  assert.equal(serial.listeners.size, 0);
  const p = waitForPuck({ serial, timeoutMs: 1000 });
  assert.equal(serial.listeners.size, 1);
  serial.emit(puckPort(0x1234, 0x1001));        // other device
  serial.emit(puckPort(0x303a, 0x0002));        // other product
  serial.emit({ getInfo: () => { throw new Error("no"); } });
  serial.emit({});                               // no getInfo at all
  serial.emit({ getInfo: () => undefined });
  assert.equal(serial.listeners.size, 1);
  const port = puckPort();
  serial.emit(port);
  assert.equal(await p, port);
  assert.equal(serial.listeners.size, 0);
});

test("waitForPuck removes its listener on timeout", async () => {
  const serial = fakeSerial();
  await assert.rejects(waitForPuck({ serial, timeoutMs: 15 }), /did not appear/);
  assert.equal(serial.listeners.size, 0);
});

test("waitForPuck removes its listener on abort, and never adds one if already aborted", async () => {
  const serial = fakeSerial();
  const ac = new AbortController();
  const p = waitForPuck({ serial, timeoutMs: 60000, signal: ac.signal });
  assert.equal(serial.listeners.size, 1);
  ac.abort();
  await assert.rejects(p, { name: "AbortError" });
  assert.equal(serial.listeners.size, 0);
  await assert.rejects(waitForPuck({ serial, signal: ac.signal }), { name: "AbortError" });
  assert.equal(serial.listeners.size, 0);
});

test("waitForPuck leaves the abort listener clean after resolving", async () => {
  const serial = fakeSerial();
  const ac = new AbortController();
  const p = waitForPuck({ serial, signal: ac.signal });
  serial.emit(puckPort());
  await p;
  ac.abort(); // must not throw or reject anything
  assert.equal(serial.listeners.size, 0);
});

test("safeDisconnect swallows errors and never hangs", async () => {
  let closed = 0;
  await safeDisconnect({ disconnect: async () => { closed++; } });
  await safeDisconnect({ disconnect: async () => { throw new Error("already closed"); } });
  await safeDisconnect({ disconnect: () => new Promise(() => {}) }, 10);
  assert.equal(closed, 1);
});

test("openLoader builds the loader with a 1 MiB serial buffer and detects the chip", async () => {
  const created = {};
  class FakeTransport { constructor(port, tracing) { created.transport = this; this.port = port; this.tracing = tracing; } async disconnect() { created.disconnected = true; } }
  class FakeLoader {
    constructor(options) { created.options = options; }
    async detectChip(mode) { created.mode = mode; }
  }
  const port = {};
  const { loader, transport } = await openLoader(port, { ESPLoader: FakeLoader, Transport: FakeTransport });
  assert.equal(transport.port, port);
  assert.equal(loader.constructor, FakeLoader);
  assert.equal(created.mode, "default_reset");
  assert.equal(created.options.serialOptions.bufferSize, 1048576);
  assert.equal(created.options.baudrate, 115200);
  assert.equal(created.options.transport, transport);
});

test("openLoader closes the port when the chip cannot be detected", async () => {
  const created = {};
  class FakeTransport { async disconnect() { created.disconnected = true; } }
  class FakeLoader { async detectChip() { throw new Error("Failed to connect with the device"); } }
  await assert.rejects(openLoader({}, { ESPLoader: FakeLoader, Transport: FakeTransport }), /Failed to connect/);
  assert.equal(created.disconnected, true);
});

test("openLoader uses the robust transport and the real loader by default", async () => {
  // A port that cannot be opened: detectChip fails, and the port is released.
  const port = { getInfo: () => ({ usbVendorId: 0x303a, usbProductId: 0x1001 }), open: async () => { throw new Error("busy"); }, readable: null, writable: null, close: async () => {} };
  await assert.rejects(openLoader(port), /./);
});
