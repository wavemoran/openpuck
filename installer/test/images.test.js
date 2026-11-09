import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import {
  backupFileName, checkBackup, dataPartitions, fetchFirmware, parseFirmwareManifest,
  parsePartitionTable, planInstall,
} from "../js/images.js";
import { FLASH_SIZE } from "../js/constants.js";

function entry(type, subtype, offset, size, label) {
  const e = new Uint8Array(32).fill(0);
  e.set([0xaa, 0x50, type, subtype]);
  const dv = new DataView(e.buffer);
  dv.setUint32(4, offset, true);
  dv.setUint32(8, size, true);
  e.set(new TextEncoder().encode(label), 12);
  return e;
}
function table(entries, total = 0xc00) {
  const t = new Uint8Array(total).fill(0xff);
  entries.forEach((e, i) => t.set(e, i * 32));
  return t;
}
const OPENPUCK_TABLE = () => table([
  entry(1, 2, 0x9000, 0x6000, "nvs"),
  entry(1, 1, 0xf000, 0x1000, "phy_init"),
  entry(0, 0, 0x10000, 0x300000, "factory"),
]);
function backupWith(tbl) {
  const b = new Uint8Array(FLASH_SIZE).fill(0xff);
  b[0] = 0xe9;
  b.set(tbl, 0x8000);
  return b;
}

test("checkBackup accepts a plausible backup", () => {
  assert.deepEqual(checkBackup(backupWith(OPENPUCK_TABLE())), { ok: true, problems: [] });
});

test("checkBackup rejects wrong size, missing bootloader and missing table", () => {
  assert.match(checkBackup(new Uint8Array(100)).problems[0], /size is 100/);
  assert.match(checkBackup(null).problems[0], /size is 0/);
  const noBoot = backupWith(OPENPUCK_TABLE()); noBoot[0] = 0;
  assert.match(checkBackup(noBoot).problems[0], /bootloader/);
  const noTable = backupWith(OPENPUCK_TABLE()); noTable[0x8001] = 0;
  assert.match(checkBackup(noTable).problems[0], /partition table/);
  const noTable2 = backupWith(OPENPUCK_TABLE()); noTable2[0x8000] = 0;
  assert.equal(checkBackup(noTable2).ok, false);
});

test("parsePartitionTable reads entries and stops at the first non-entry", () => {
  const t = OPENPUCK_TABLE();
  const entries = parsePartitionTable(t);
  assert.equal(entries.length, 3);
  assert.deepEqual(entries[0], { type: 1, subtype: 2, offset: 0x9000, size: 0x6000, label: "nvs" });
  assert.equal(entries[2].label, "factory");
  assert.deepEqual(parsePartitionTable(new Uint8Array(0)), []);
  assert.deepEqual(parsePartitionTable(new Uint8Array(40).fill(0xff)), []);
  // A trailing partial entry is ignored.
  assert.equal(parsePartitionTable(t.subarray(0, 32 * 3 + 5)).length, 3);
  // Works on a view with a non-zero byteOffset.
  const big = new Uint8Array(0x9000); big.set(t, 0x8000);
  assert.equal(parsePartitionTable(big.subarray(0x8000)).length, 3);
});

test("dataPartitions returns only type 1 entries", () => {
  assert.deepEqual(dataPartitions(OPENPUCK_TABLE()), [
    { offset: 0x9000, size: 0x6000, label: "nvs" },
    { offset: 0xf000, size: 0x1000, label: "phy_init" },
  ]);
});

const parts = (tbl) => [
  { offset: 0, path: "bootloader.bin", data: new Uint8Array([0xe9, 1, 2, 3]) },
  { offset: 0x8000, path: "partition-table.bin", data: tbl },
  { offset: 0x10000, path: "openpuck.bin", data: new Uint8Array([0xe9, 9, 9, 9]) },
];

test("planInstall: same layout writes only the three parts", () => {
  const tbl = OPENPUCK_TABLE();
  const plan = planInstall({ backup: backupWith(tbl), parts: parts(tbl) });
  assert.equal(plan.layoutChanged, false);
  assert.deepEqual(plan.steps.map((s) => [s.kind, s.address]), [["write", 0], ["write", 0x8000], ["write", 0x10000]]);
});

test("planInstall: different layout clears the data partitions of the NEW table first", () => {
  const old = table([entry(0, 0, 0x10000, 0x100000, "factory")]);
  const tbl = OPENPUCK_TABLE();
  const plan = planInstall({ backup: backupWith(old), parts: parts(tbl) });
  assert.equal(plan.layoutChanged, true);
  assert.deepEqual(plan.steps.map((s) => [s.kind, s.address, s.data.length]), [
    ["erase", 0x9000, 0x6000], ["erase", 0xf000, 0x1000],
    ["write", 0, 4], ["write", 0x8000, 0xc00], ["write", 0x10000, 4],
  ]);
  assert.ok(plan.steps[0].data.every((b) => b === 0xff));
});

test("planInstall: a short new table is compared as if padded with 0xFF", () => {
  const short = OPENPUCK_TABLE().subarray(0, 0x400);
  const plan = planInstall({ backup: backupWith(OPENPUCK_TABLE()), parts: parts(short) });
  assert.equal(plan.layoutChanged, false);
});

test("planInstall refuses bad input", () => {
  const tbl = OPENPUCK_TABLE();
  assert.throws(() => planInstall({ backup: backupWith(tbl), parts: parts(tbl).filter((p) => p.offset !== 0x8000) }), /partition table/);
  assert.throws(() => planInstall({ backup: new Uint8Array(10), parts: parts(tbl) }), /Not a full flash backup/);
  const huge = table([entry(1, 2, FLASH_SIZE - 0x1000, 0x6000, "nvs")]);
  assert.throws(() => planInstall({ backup: backupWith(OPENPUCK_TABLE()), parts: parts(huge) }), /does not fit/);
});

test("planInstall skips zero-sized data partitions and unlabeled ones get a generic label", () => {
  const t = table([entry(1, 2, 0x9000, 0, "empty"), entry(1, 2, 0xa000, 0x1000, "")]);
  const plan = planInstall({ backup: backupWith(OPENPUCK_TABLE()), parts: parts(t) });
  const erases = plan.steps.filter((s) => s.kind === "erase");
  assert.equal(erases.length, 1);
  assert.match(erases[0].label, /Clearing data/);
});

test("backupFileName formats time and MAC", () => {
  const d = new Date(2026, 10, 9, 5, 7, 3);
  assert.equal(backupFileName("AA:BB:CC:DD:EE:0F", d), "life-puck-backup-aabbccddee0f-20261109-050703.bin");
  assert.equal(backupFileName(new Uint8Array([1, 2, 255, 0, 0, 16]), d), "life-puck-backup-0102ff000010-20261109-050703.bin");
  assert.equal(backupFileName("", d), "life-puck-backup-20261109-050703.bin");
  assert.match(backupFileName("aa:bb:cc:dd:ee:ff"), /^life-puck-backup-aabbccddeeff-\d{8}-\d{6}\.bin$/);
});

const SHA = "a".repeat(64);
const goodManifest = () => ({
  name: "OpenPuck", version: "0.1.0", chip: "ESP32-C3",
  parts: [
    { offset: 0, path: "bootloader.bin", size: 20000, sha256: SHA },
    { offset: 32768, path: "partition-table.bin", size: 3072, sha256: SHA.toUpperCase() },
    { offset: 65536, path: "openpuck.bin", size: 600000, sha256: SHA },
  ],
});

test("parseFirmwareManifest accepts a good manifest and lowercases hashes", () => {
  const m = parseFirmwareManifest(goodManifest());
  assert.equal(m.parts.length, 3);
  assert.equal(m.parts[1].sha256, SHA);
  assert.equal(m.version, "0.1.0");
});

test("parseFirmwareManifest rejects bad manifests", () => {
  const mutate = (fn) => { const m = goodManifest(); fn(m); return m; };
  const bad = [
    [null, /not an object/],
    [[], /not an object/],
    [mutate((m) => { m.name = ""; }), /name/],
    [mutate((m) => { delete m.version; }), /version/],
    [mutate((m) => { m.chip = "ESP32"; }), /chip/],
    [mutate((m) => { m.parts = []; }), /no parts/],
    [mutate((m) => { m.parts[0] = null; }), /not an object/],
    [mutate((m) => { m.parts[0].offset = -4; }), /offset/],
    [mutate((m) => { m.parts[0].offset = 2; }), /offset/],
    [mutate((m) => { m.parts[0].path = "../x.bin"; }), /path/],
    [mutate((m) => { m.parts[0].path = "/etc/passwd"; }), /path/],
    [mutate((m) => { m.parts[0].path = "a..b"; }), /path/],
    [mutate((m) => { m.parts[0].path = "https://x/y.bin"; }), /path/],
    [mutate((m) => { m.parts[0].size = 0; }), /size/],
    [mutate((m) => { m.parts[2].size = FLASH_SIZE; }), /fit/],
    [mutate((m) => { m.parts[0].sha256 = "xyz"; }), /sha256/],
    [mutate((m) => { m.parts[0].size = 40000; }), /overlap/],
    [mutate((m) => { m.parts.splice(1, 1); }), /0x8000/],
  ];
  for (const [input, re] of bad) assert.throws(() => parseFirmwareManifest(input), re);
});

function fakeFetch(files, overrides = {}) {
  return async (url) => {
    const name = url.replace(/^firmware\//, "");
    if (overrides[name] === "missing" || !(name in files)) return { ok: false, status: 404 };
    const body = files[name];
    return {
      ok: true, status: 200,
      json: async () => JSON.parse(new TextDecoder().decode(body)),
      arrayBuffer: async () => body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength),
    };
  };
}
function firmwareFiles() {
  const data = {
    "bootloader.bin": new Uint8Array([0xe9, 1, 2, 3, 4]),
    "partition-table.bin": OPENPUCK_TABLE(),
    "openpuck.bin": new Uint8Array([0xe9, 5, 6, 7]),
  };
  const sha = (d) => createHash("sha256").update(d).digest("hex");
  const manifest = {
    name: "OpenPuck", version: "1.2.3", chip: "ESP32-C3",
    parts: [
      { offset: 0, path: "bootloader.bin", size: 5, sha256: sha(data["bootloader.bin"]) },
      { offset: 0x8000, path: "partition-table.bin", size: 0xc00, sha256: sha(data["partition-table.bin"]) },
      { offset: 0x10000, path: "openpuck.bin", size: 4, sha256: sha(data["openpuck.bin"]) },
    ],
  };
  return { ...data, "manifest.json": new TextEncoder().encode(JSON.stringify(manifest)) };
}

test("fetchFirmware downloads and verifies every part", async () => {
  const fw = await fetchFirmware(fakeFetch(firmwareFiles()));
  assert.equal(fw.manifest.version, "1.2.3");
  assert.deepEqual(fw.parts.map((p) => p.offset), [0, 0x8000, 0x10000]);
  assert.equal(fw.parts[2].data[1], 5);
});

test("fetchFirmware refuses a missing file, wrong size and wrong hash", async () => {
  const files = firmwareFiles();
  await assert.rejects(fetchFirmware(fakeFetch(files, { "openpuck.bin": "missing" })), /HTTP 404/);
  await assert.rejects(fetchFirmware(fakeFetch({ ...files, "openpuck.bin": new Uint8Array(9) })), /wrong size/);
  await assert.rejects(fetchFirmware(fakeFetch({ ...files, "openpuck.bin": new Uint8Array([0xe9, 5, 6, 8]) })), /checksum/);
  await assert.rejects(fetchFirmware(fakeFetch({})), /manifest.json/);
});
