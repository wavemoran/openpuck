// Pure helpers for flash images: backup checks, partition tables, install planning,
// and the firmware manifest.
import {
  FLASH_SIZE, IMAGE_MAGIC, PARTITION_ENTRY_SIZE, PARTITION_MAGIC, PARTITION_TABLE_COMPARE_LEN,
  PARTITION_TABLE_OFFSET, PARTITION_TYPE_DATA,
} from "./constants.js";
import { sha256Hex } from "./hash.js";

/**
 * Check that bytes look like a full flash backup (same checks as tools/flash.py).
 * @param {Uint8Array} data
 * @returns {{ok: boolean, problems: string[]}}
 */
export function checkBackup(data) {
  const problems = [];
  if (!data || data.length !== FLASH_SIZE) {
    problems.push(`size is ${data ? data.length : 0} bytes, expected ${FLASH_SIZE}`);
  } else if (data[0] !== IMAGE_MAGIC) {
    problems.push("no bootloader image at offset 0");
  } else if (data[PARTITION_TABLE_OFFSET] !== PARTITION_MAGIC[0] ||
             data[PARTITION_TABLE_OFFSET + 1] !== PARTITION_MAGIC[1]) {
    problems.push("no partition table at 0x8000");
  }
  return { ok: problems.length === 0, problems };
}

/**
 * Parse a partition table image into entries, stopping at the first entry
 * without the 0xAA50 magic.
 * @param {Uint8Array} table
 * @returns {{type: number, subtype: number, offset: number, size: number, label: string}[]}
 */
export function parsePartitionTable(table) {
  const entries = [];
  const dv = new DataView(table.buffer, table.byteOffset, table.byteLength);
  for (let i = 0; i + PARTITION_ENTRY_SIZE <= table.length; i += PARTITION_ENTRY_SIZE) {
    if (table[i] !== PARTITION_MAGIC[0] || table[i + 1] !== PARTITION_MAGIC[1]) break;
    let label = "";
    for (let j = i + 12; j < i + 28 && table[j] !== 0; j++) label += String.fromCharCode(table[j]);
    entries.push({
      type: table[i + 2],
      subtype: table[i + 3],
      offset: dv.getUint32(i + 4, true),
      size: dv.getUint32(i + 8, true),
      label,
    });
  }
  return entries;
}

/**
 * Data partitions (type 1) of a partition table image.
 * @param {Uint8Array} table
 * @returns {{offset: number, size: number, label: string}[]}
 */
export function dataPartitions(table) {
  return parsePartitionTable(table)
    .filter((e) => e.type === PARTITION_TYPE_DATA)
    .map(({ offset, size, label }) => ({ offset, size, label }));
}

function sameBytes(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

/**
 * Work out what to write. Mirrors tools/flash.py cmd_flash: if the partition
 * table on the puck differs from the new one, every data partition of the new
 * table is cleared first (written as 0xFF).
 * @param {{backup: Uint8Array, parts: {offset: number, data: Uint8Array, path?: string}[]}} input
 * @returns {{layoutChanged: boolean, steps: {kind: "erase"|"write", label: string, address: number, data: Uint8Array}[]}}
 */
export function planInstall({ backup, parts }) {
  const table = parts.find((p) => p.offset === PARTITION_TABLE_OFFSET);
  if (!table) throw new Error("The firmware has no partition table at 0x8000.");
  const check = checkBackup(backup);
  if (!check.ok) throw new Error(`Not a full flash backup: ${check.problems.join("; ")}`);

  const wanted = new Uint8Array(PARTITION_TABLE_COMPARE_LEN).fill(0xff);
  wanted.set(table.data.subarray(0, PARTITION_TABLE_COMPARE_LEN));
  const current = backup.subarray(PARTITION_TABLE_OFFSET, PARTITION_TABLE_OFFSET + PARTITION_TABLE_COMPARE_LEN);
  const layoutChanged = !sameBytes(current, wanted);

  const steps = [];
  if (layoutChanged) {
    for (const p of dataPartitions(table.data)) {
      if (p.size === 0) continue;
      if (p.offset + p.size > FLASH_SIZE) {
        throw new Error(`Partition ${p.label || p.offset.toString(16)} does not fit in flash.`);
      }
      steps.push({
        kind: "erase",
        label: `Clearing ${p.label || "data"} at 0x${p.offset.toString(16)}`,
        address: p.offset,
        data: new Uint8Array(p.size).fill(0xff),
      });
    }
  }
  for (const p of parts) {
    steps.push({
      kind: "write",
      label: `Writing ${p.path ?? "image"} at 0x${p.offset.toString(16)}`,
      address: p.offset,
      data: p.data,
    });
  }
  return { layoutChanged, steps };
}

const pad2 = (n) => String(n).padStart(2, "0");

/**
 * File name for a backup download.
 * @param {string|ArrayLike<number>} mac "aa:bb:cc:dd:ee:ff" or 6 bytes
 * @param {Date} [date] local time is used
 * @returns {string} e.g. life-puck-backup-aabbccddeeff-20261109-153000.bin
 */
export function backupFileName(mac, date = new Date()) {
  const hex = typeof mac === "string"
    ? mac.toLowerCase().replace(/[^0-9a-f]/g, "")
    : Array.from(mac, (b) => b.toString(16).padStart(2, "0")).join("");
  const stamp = `${date.getFullYear()}${pad2(date.getMonth() + 1)}${pad2(date.getDate())}` +
    `-${pad2(date.getHours())}${pad2(date.getMinutes())}${pad2(date.getSeconds())}`;
  return `life-puck-backup-${hex ? hex + "-" : ""}${stamp}.bin`;
}

/**
 * Validate a firmware manifest object and return a normalized copy.
 * @param {unknown} json
 * @returns {{name: string, version: string, chip: string,
 *   parts: {offset: number, path: string, size: number, sha256: string}[]}}
 */
export function parseFirmwareManifest(json) {
  const fail = (msg) => { throw new Error(`Invalid firmware manifest: ${msg}`); };
  if (!json || typeof json !== "object" || Array.isArray(json)) fail("not an object");
  const { name, version, chip, parts } = json;
  if (typeof name !== "string" || !name) fail("missing name");
  if (typeof version !== "string" || !version) fail("missing version");
  if (chip !== "ESP32-C3") fail(`unsupported chip ${JSON.stringify(chip)}`);
  if (!Array.isArray(parts) || parts.length === 0) fail("no parts");
  const out = parts.map((p, i) => {
    if (!p || typeof p !== "object") fail(`part ${i} is not an object`);
    const { offset, path, size, sha256 } = p;
    if (!Number.isInteger(offset) || offset < 0 || offset % 4 !== 0) fail(`part ${i} has a bad offset`);
    if (typeof path !== "string" || !/^[A-Za-z0-9][A-Za-z0-9._-]*$/.test(path) || path.includes("..")) {
      fail(`part ${i} has a bad path`);
    }
    if (!Number.isInteger(size) || size <= 0) fail(`part ${i} has a bad size`);
    if (offset + size > FLASH_SIZE) fail(`part ${i} does not fit in flash`);
    if (typeof sha256 !== "string" || !/^[0-9a-f]{64}$/i.test(sha256)) fail(`part ${i} has a bad sha256`);
    return { offset, path, size, sha256: sha256.toLowerCase() };
  });
  const sorted = [...out].sort((a, b) => a.offset - b.offset);
  for (let i = 1; i < sorted.length; i++) {
    if (sorted[i - 1].offset + sorted[i - 1].size > sorted[i].offset) fail("parts overlap");
  }
  if (!out.some((p) => p.offset === PARTITION_TABLE_OFFSET)) fail("no partition table at 0x8000");
  return { name, version, chip, parts: out };
}

/**
 * Fetch the manifest and every part from the same origin and verify size and SHA-256.
 * @param {(url: string) => Promise<{ok: boolean, status: number, json: Function, arrayBuffer: Function}>} fetchFn
 * @param {string} [baseUrl] directory holding manifest.json, with trailing slash
 * @returns {Promise<{manifest: object, parts: {offset: number, path: string, data: Uint8Array}[]}>}
 */
export async function fetchFirmware(fetchFn, baseUrl = "firmware/") {
  const get = async (name) => {
    const res = await fetchFn(baseUrl + name);
    if (!res.ok) throw new Error(`Could not download ${name} (HTTP ${res.status}).`);
    return res;
  };
  const manifest = parseFirmwareManifest(await (await get("manifest.json")).json());
  const parts = [];
  for (const p of manifest.parts) {
    const data = new Uint8Array(await (await get(p.path)).arrayBuffer());
    if (data.length !== p.size) {
      throw new Error(`${p.path} has the wrong size (${data.length}, expected ${p.size}).`);
    }
    if ((await sha256Hex(data)) !== p.sha256) {
      throw new Error(`${p.path} failed its checksum.`);
    }
    parts.push({ offset: p.offset, path: p.path, data });
  }
  return { manifest, parts };
}
