import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { connectPuck, defaultDeps, runInstall, runRestore } from "../js/flow.js";
import { FLASH_SIZE } from "../js/constants.js";

// ------------------------------------------------------------------ fixtures
function entry(type, subtype, offset, size, label) {
  const e = new Uint8Array(32);
  e.set([0xaa, 0x50, type, subtype]);
  const dv = new DataView(e.buffer);
  dv.setUint32(4, offset, true);
  dv.setUint32(8, size, true);
  e.set(new TextEncoder().encode(label), 12);
  return e;
}
function table(entries) {
  const t = new Uint8Array(0xc00).fill(0xff);
  entries.forEach((e, i) => t.set(e, i * 32));
  return t;
}
const NEW_TABLE = table([
  entry(1, 2, 0x9000, 0x6000, "nvs"),
  entry(1, 1, 0xf000, 0x1000, "phy_init"),
  entry(0, 0, 0x10000, 0x300000, "factory"),
]);
const OLD_TABLE = table([entry(0, 0, 0x10000, 0x100000, "factory")]);
function backupWith(tbl) {
  const b = new Uint8Array(FLASH_SIZE).fill(0xff);
  b[0] = 0xe9;
  b.set(tbl, 0x8000);
  return b;
}
const FIRMWARE = {
  manifest: { name: "OpenPuck", version: "0.1.0" },
  parts: [
    { offset: 0, path: "bootloader.bin", data: new Uint8Array([0xe9, 1, 2, 3]) },
    { offset: 0x8000, path: "partition-table.bin", data: NEW_TABLE },
    { offset: 0x10000, path: "openpuck.bin", data: new Uint8Array([0xe9, 4, 5, 6]) },
  ],
};

// A fake world. `log` records every interesting call in order.
function makeWorld(options = {}) {
  const {
    openResults = [{ ok: true }],
    backup = backupWith(NEW_TABLE),
    flashId = 0x164020,
    held = [false],
    failWrite = () => false,
    failRelease = false,
    waitForPuckImpl,
    fetchImpl,
  } = options;
  const flashSize = options.flashSize ?? FLASH_SIZE;
  const log = [];
  const opens = [...openResults];
  const heldStates = [...held];
  let confirmResolve;
  const gate = options.manualConfirm ? new Promise((r) => { confirmResolve = r; }) : null;
  const ports = { first: { name: "first" }, second: { name: "second" } };
  const ui = {
    show: (name, details) => log.push(["show", name, details]),
    progress: (done, total, label) => log.push(["progress", done, total, label]),
    cue: (c) => log.push(["cue", c]),
    saveBackup: (name, bytes, sha) => log.push(["saveBackup", name, bytes.length, sha]),
    confirmBackupSaved: async () => {
      log.push(["confirm:start"]);
      if (options.confirmRejects) throw new Error("user closed the page");
      if (gate) await gate;
      log.push(["confirm:done"]);
    },
  };
  let connCount = 0;
  const deps = {
    fetchFirmware: fetchImpl ?? (async () => { log.push(["fetchFirmware"]); return FIRMWARE; }),
    requestPort: async () => { log.push(["requestPort"]); if (options.noPort) throw new Error("No port selected"); return ports.first; },
    openLoader: async (port) => {
      const r = opens.shift() ?? { ok: true };
      log.push(["openLoader", port.name, r.ok]);
      if (!r.ok) throw new Error("Failed to connect with the device");
      const n = ++connCount;
      return { loader: { id: `loader${n}` }, transport: { id: `transport${n}` } };
    },
    holdPower: async (l) => { log.push(["holdPower", l.id]); },
    isButtonHeld: async (l) => { log.push(["isButtonHeld", l.id]); return heldStates.length > 1 ? heldStates.shift() : heldStates[0]; },
    waitForButtonRelease: async (l, o) => {
      log.push(["waitForButtonRelease", l.id, o.timeoutMs]);
      if (failRelease) throw new Error("The power button is still held after 30 seconds.");
    },
    waitForPuck: async (o) => {
      log.push(["waitForPuck", o.timeoutMs]);
      if (waitForPuckImpl) return waitForPuckImpl(o);
      return ports.second;
    },
    safeDisconnect: async (t) => { log.push(["safeDisconnect", t.id]); },
    resetToApp: async (t) => { log.push(["resetToApp", t.id]); if (options.resetThrows) throw new Error("reset failed"); },
    prepareFlash: async (l) => { log.push(["prepareFlash", l.id]); return { flashId, mac: "aa:bb:cc:dd:ee:ff", flashSize }; },
    readFlashVerified: async (l, addr, size, o) => {
      log.push(["read", addr, size]);
      o.onProgress(size, size);
      return backup;
    },
    writeImagesVerified: async (l, images, o) => {
      const im = images[0];
      log.push(["write", l.id, im.address, im.data.length]);
      o.onProgress(im.data.length, im.data.length, "x");
      if (failWrite(im, log.filter((e) => e[0] === "write").length)) throw new Error("Verification failed at 0x" + im.address.toString(16));
    },
    sha256Hex: async (b) => createHash("sha256").update(b).digest("hex"),
    sleep: async (ms) => { log.push(["sleep", ms]); },
  };
  return { ui, deps, log, resolveConfirm: () => confirmResolve(), ports };
}
const names = (log) => log.map((e) => e[0] === "show" || e[0] === "cue" ? `${e[0]}:${e[1]}` : e[0]);
const writes = (log) => log.filter((e) => e[0] === "write").map((e) => [e[2], e[3]]);
const count = (log, name) => log.filter((e) => e[0] === name).length;
const last = (log, name) => log.filter((e) => e[0] === "show" && e[1] === name).at(-1)?.[2];

// ---------------------------------------------------------------- connectPuck
test("connectPuck: a puck running OpenPuck connects directly", async () => {
  const w = makeWorld({ held: [false] });
  const r = await connectPuck(w.ports.first, { ui: w.ui, deps: w.deps });
  assert.equal(r.runningOpenPuck, true);
  assert.equal(r.loader.id, "loader1");
  assert.equal(count(w.log, "holdPower"), 1);
  assert.equal(count(w.log, "waitForPuck"), 0);
  assert.equal(count(w.log, "waitForButtonRelease"), 0);
  assert.equal(count(w.log, "resetToApp"), 0);
});

test("connectPuck: if the button is held, ask for release", async () => {
  const w = makeWorld({ held: [true] });
  const r = await connectPuck(w.ports.first, { ui: w.ui, deps: w.deps });
  assert.equal(r.runningOpenPuck, true);
  assert.deepEqual(names(w.log).filter((n) => /cue|Release/.test(n)), ["cue:release", "waitForButtonRelease"]);
  assert.equal(w.log.find((e) => e[0] === "waitForButtonRelease")[2], 30000);
});

test("connectPuck: the original software path waits for the puck, then guides the release", async () => {
  const w = makeWorld({ openResults: [{ ok: false }, { ok: true }] });
  const session = { conn: null };
  const r = await connectPuck(w.ports.first, { ui: w.ui, deps: w.deps, session });
  assert.equal(r.runningOpenPuck, false);
  assert.equal(session.conn.loader.id, "loader1");
  assert.deepEqual(names(w.log), [
    "show:connecting", "openLoader", "show:hold", "cue:hold", "waitForPuck", "sleep",
    "openLoader", "holdPower", "show:release", "cue:release", "waitForButtonRelease", "sleep",
  ]);
  assert.equal(w.log.find((e) => e[0] === "waitForPuck")[1], 600000);
  assert.deepEqual(w.log.filter((e) => e[0] === "sleep").map((e) => e[1]), [2000, 500]);
  assert.deepEqual(w.log.filter((e) => e[0] === "openLoader").map((e) => e[1]), ["first", "second"]);
});

test("connectPuck: abort while waiting for the puck propagates", async () => {
  const w = makeWorld({
    openResults: [{ ok: false }],
    waitForPuckImpl: async ({ signal }) => { throw Object.assign(new Error("Canceled"), { name: "AbortError" }); },
  });
  await assert.rejects(connectPuck(w.ports.first, { ui: w.ui, deps: w.deps }), { name: "AbortError" });
});

// ----------------------------------------------------------------- runInstall
test("install on a puck running OpenPuck: whole sequence in order", async () => {
  const w = makeWorld();
  const r = await runInstall({ ui: w.ui, deps: w.deps, now: () => new Date(2026, 10, 9, 12, 0, 0) });
  assert.equal(r.ok, true);
  assert.equal(r.fileName, "life-puck-backup-aabbccddeeff-20261109-120000.bin");
  assert.equal(r.backupSha256.length, 64);
  assert.deepEqual(writes(w.log), [[0, 4], [0x8000, 0xc00], [0x10000, 4]]);
  const seq = names(w.log).filter((n) => !/^(progress)$/.test(n));
  assert.deepEqual(seq, [
    "show:firmware", "fetchFirmware", "show:connect", "requestPort", "show:connecting", "openLoader",
    "holdPower", "isButtonHeld", "show:prepare", "prepareFlash", "show:backup", "read",
    "show:save-backup", "saveBackup", "confirm:start", "confirm:done",
    "show:writing", "write", "write", "write", "show:finishing", "resetToApp", "safeDisconnect",
    "cue:done", "show:done",
  ]);
  assert.equal(last(w.log, "done").kind, "install");
  assert.equal(last(w.log, "done").layoutChanged, false);
  assert.equal(last(w.log, "done").startsByItself, true);
  const progress = w.log.filter((e) => e[0] === "progress");
  assert.ok(progress.length > 0);
  assert.equal(progress.at(-1)[1], progress.at(-1)[2]);
});

test("install on the original software: hold, release, backup, write, reset", async () => {
  const w = makeWorld({ openResults: [{ ok: false }, { ok: true }], backup: backupWith(OLD_TABLE) });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, true);
  const n = names(w.log);
  assert.ok(n.indexOf("cue:hold") < n.indexOf("waitForPuck"));
  assert.ok(n.indexOf("cue:release") < n.indexOf("read"));
  assert.equal(count(w.log, "resetToApp"), 1);
  assert.equal(w.log.find((e) => e[0] === "resetToApp")[1], "transport1");
});

test("nothing is written or erased before confirmBackupSaved resolves", async () => {
  const w = makeWorld({ manualConfirm: true, backup: backupWith(OLD_TABLE) });
  const pending = runInstall({ ui: w.ui, deps: w.deps });
  for (let i = 0; i < 50; i++) await new Promise((r) => setImmediate(r));
  assert.ok(names(w.log).includes("confirm:start"));
  assert.equal(count(w.log, "saveBackup"), 1);
  assert.equal(count(w.log, "write"), 0, "no write/erase while the user has not confirmed");
  assert.equal(count(w.log, "resetToApp"), 0);
  w.resolveConfirm();
  const r = await pending;
  assert.equal(r.ok, true);
  const n = names(w.log);
  assert.ok(n.indexOf("confirm:done") < n.indexOf("write"));
  assert.equal(n.indexOf("saveBackup") < n.indexOf("confirm:start"), true);
});

test("if the user never confirms (error), nothing is written and the puck is reset", async () => {
  const w = makeWorld({ confirmRejects: true });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(r.writeStarted, false);
  assert.equal(count(w.log, "write"), 0);
  assert.equal(count(w.log, "resetToApp"), 1);
});

test("a changed partition table clears the new data partitions first, after the confirmation", async () => {
  const w = makeWorld({ backup: backupWith(OLD_TABLE) });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, true);
  assert.deepEqual(writes(w.log), [[0x9000, 0x6000], [0xf000, 0x1000], [0, 4], [0x8000, 0xc00], [0x10000, 4]]);
  assert.equal(last(w.log, "done").layoutChanged, true);
});

test("a different 4 MB flash manufacturer is accepted", async () => {
  const w = makeWorld({ flashId: 0x164046 });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, true);
  assert.equal(count(w.log, "read"), 1);
  assert.equal(count(w.log, "write"), 3);
});

test("wrong flash capacity: nothing is read or written, the puck is reset, error shown", async () => {
  const w = makeWorld({ flashId: 0x1740c8 });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(r.writeStarted, false);
  assert.equal(r.resume, null);
  assert.match(r.error.message, /unexpected flash chip \(ID 0x1740c8\)/);
  assert.equal(count(w.log, "read"), 0);
  assert.equal(count(w.log, "write"), 0);
  assert.equal(count(w.log, "saveBackup"), 0);
  assert.deepEqual(names(w.log).slice(-4), ["resetToApp", "safeDisconnect", "cue:error", "show:error"]);
  const e = last(w.log, "error");
  assert.equal(e.writeStarted, false);
  assert.equal(e.canRetry, true);
  assert.equal(e.dontUnplug, false);
});

test("invalid flash ID bytes are rejected before reads or writes", async () => {
  for (const flashId of [0xffffff, 0x000000, 0x16ffff, 0x160000, 0x364046]) {
    const w = makeWorld({ flashId });
    const r = await runInstall({ ui: w.ui, deps: w.deps });
    assert.equal(r.ok, false, flashId.toString(16));
    assert.match(r.error.message, new RegExp(`unexpected flash chip \\(ID 0x${flashId.toString(16)}\\)`));
    assert.equal(count(w.log, "read"), 0);
    assert.equal(count(w.log, "write"), 0);
  }
});

test("a bad backup (checkBackup fails): nothing is saved or written", async () => {
  for (const bad of [new Uint8Array(1234), (() => { const b = backupWith(NEW_TABLE); b[0] = 0; return b; })()]) {
    const w = makeWorld({ backup: bad });
    const r = await runInstall({ ui: w.ui, deps: w.deps });
    assert.equal(r.ok, false);
    assert.match(r.error.message, /backup doesn't look right.*Nothing was written/);
    assert.equal(count(w.log, "saveBackup"), 0);
    assert.equal(count(w.log, "confirm:start"), 0);
    assert.equal(count(w.log, "write"), 0);
    assert.equal(count(w.log, "resetToApp"), 1);
  }
});

test("errors before connecting do not touch the device", async () => {
  let w = makeWorld({ fetchImpl: async () => { throw new Error("firmware.bin failed its checksum."); } });
  let r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "openLoader"), 0);
  assert.equal(count(w.log, "resetToApp"), 0);
  assert.equal(last(w.log, "error").message, "firmware.bin failed its checksum.");

  w = makeWorld({ noPort: true });
  r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "openLoader"), 0);
});

test("a failure while connecting after the loader opened resets the puck", async () => {
  const w = makeWorld({ openResults: [{ ok: false }, { ok: true }], failRelease: true });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "write"), 0);
  assert.equal(count(w.log, "resetToApp"), 1);
  assert.equal(count(w.log, "read"), 0);
});

test("a reset that fails while reporting an error is ignored", async () => {
  const w = makeWorld({ flashId: 1, resetThrows: true });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "safeDisconnect"), 1);
});

test("abort is reported as aborted", async () => {
  const w = makeWorld({ fetchImpl: async () => { throw Object.assign(new Error("Canceled"), { name: "AbortError" }); } });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(last(w.log, "error").aborted, true);
});

test("non-Error failures are reported as text", async () => {
  const w = makeWorld({ fetchImpl: async () => { throw "plain string"; } });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(last(w.log, "error").message, "plain string");
  assert.equal(r.ok, false);
});

test("a write failure: no reset, 'don't unplug', point to Restore; retry resumes at the failed step", async () => {
  let fail = true;
  const w = makeWorld({ failWrite: (im) => fail && im.address === 0x8000 });
  const r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(r.writeStarted, true);
  assert.equal(count(w.log, "resetToApp"), 0);
  assert.equal(count(w.log, "safeDisconnect"), 0);
  const e = last(w.log, "error");
  assert.equal(e.dontUnplug, true);
  assert.equal(e.pointToRestore, true);
  assert.equal(e.canRetry, true);
  assert.deepEqual(writes(w.log), [[0, 4], [0x8000, 0xc00]]);
  assert.equal(typeof r.resume, "function");

  fail = false;
  const r2 = await r.resume();
  assert.equal(r2.ok, true);
  assert.deepEqual(writes(w.log), [[0, 4], [0x8000, 0xc00], [0x8000, 0xc00], [0x10000, 4]]);
  assert.equal(count(w.log, "resetToApp"), 1);
  assert.equal(last(w.log, "done").kind, "install");
  assert.equal(r2.fileName.startsWith("life-puck-backup-"), true);
  // No second backup, no second confirmation.
  assert.equal(count(w.log, "read"), 1);
  assert.equal(count(w.log, "confirm:start"), 1);
});

test("a failing retry can be retried again", async () => {
  let failures = 2;
  const w = makeWorld({ failWrite: () => failures-- > 0 });
  let r = await runInstall({ ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  r = await r.resume();
  assert.equal(r.ok, false);
  assert.equal(typeof r.resume, "function");
  r = await r.resume();
  assert.equal(r.ok, true);
});

// ---------------------------------------------------------------- runRestore
test("restore refuses a file that is not a full backup, without touching the device", async () => {
  const w = makeWorld();
  const r = await runRestore(new Uint8Array(500), { ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.match(r.error.message, /isn't a full backup.*Nothing was written/);
  assert.equal(count(w.log, "requestPort"), 0);
  assert.equal(count(w.log, "openLoader"), 0);
  assert.equal(count(w.log, "write"), 0);
});

test("restore writes the whole 4 MiB at 0, then resets", async () => {
  const w = makeWorld({ openResults: [{ ok: false }, { ok: true }] });
  const r = await runRestore(backupWith(OLD_TABLE), { ui: w.ui, deps: w.deps });
  assert.equal(r.ok, true);
  assert.deepEqual(writes(w.log), [[0, FLASH_SIZE]]);
  const n = names(w.log);
  assert.ok(n.indexOf("prepareFlash") < n.indexOf("write"));
  assert.ok(n.indexOf("write") < n.indexOf("resetToApp"));
  assert.equal(count(w.log, "read"), 0, "restore never reads a backup");
  assert.match(last(w.log, "done").hint, /press the power button/);
  assert.equal(last(w.log, "done").kind, "restore");
});

test("restore stops without writing on an invalid flash ID", async () => {
  const w = makeWorld({ flashId: 0xffffff });
  const r = await runRestore(backupWith(OLD_TABLE), { ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "write"), 0);
  assert.equal(count(w.log, "resetToApp"), 1);
});

test("restore write failure can be retried on the same connection", async () => {
  let fail = true;
  const w = makeWorld({ failWrite: () => fail });
  const r = await runRestore(backupWith(OLD_TABLE), { ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(r.writeStarted, true);
  assert.equal(count(w.log, "resetToApp"), 0);
  fail = false;
  const r2 = await r.resume();
  assert.equal(r2.ok, true);
  assert.equal(count(w.log, "openLoader"), 1);
  assert.equal(count(w.log, "resetToApp"), 1);
});

test("restore reports a missing port selection", async () => {
  const w = makeWorld({ noPort: true });
  const r = await runRestore(backupWith(OLD_TABLE), { ui: w.ui, deps: w.deps });
  assert.equal(r.ok, false);
  assert.equal(count(w.log, "openLoader"), 0);
});

// ---------------------------------------------------------------- defaultDeps
test("defaultDeps wires the real modules and binds serial and fetch", async () => {
  const requestPort = async () => "port";
  const files = {};
  const fetchFn = async (url) => {
    files.last = url;
    return { ok: false, status: 404 };
  };
  const deps = defaultDeps({ serial: { addEventListener() {}, removeEventListener() {} }, requestPort, fetch: fetchFn, baseUrl: "fw/" });
  for (const k of ["fetchFirmware", "requestPort", "openLoader", "holdPower", "isButtonHeld", "waitForButtonRelease",
    "waitForPuck", "safeDisconnect", "resetToApp", "prepareFlash", "readFlashVerified", "writeImagesVerified",
    "sha256Hex", "sleep"]) assert.equal(typeof deps[k], "function", k);
  assert.equal(deps.requestPort, requestPort);
  await assert.rejects(deps.fetchFirmware(), /manifest.json.*404/);
  assert.equal(files.last, "fw/manifest.json");
  const ac = new AbortController(); ac.abort();
  await assert.rejects(deps.waitForPuck({ timeoutMs: 10, signal: ac.signal }), { name: "AbortError" });
});
