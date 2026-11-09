import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { md5, toHex } from "../js/md5.js";
import { sha256Hex } from "../js/hash.js";

test("md5 known vectors", () => {
  const enc = new TextEncoder();
  assert.equal(toHex(md5(new Uint8Array(0))), "d41d8cd98f00b204e9800998ecf8427e");
  assert.equal(toHex(md5(enc.encode("abc"))), "900150983cd24fb0d6963f7d28e17f72");
  assert.equal(toHex(md5(enc.encode("The quick brown fox jumps over the lazy dog"))),
    "9e107d9d372bb6826bd81d3542a419d6");
});

test("md5 matches node:crypto for lengths around block boundaries and random lengths", () => {
  const lengths = [0, 1, 54, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 1000, 4096, 65537];
  for (let i = 0; i < 30; i++) lengths.push(Math.floor(Math.random() * 20000));
  for (const n of lengths) {
    const data = new Uint8Array(randomBytes(n));
    assert.equal(toHex(md5(data)), createHash("md5").update(data).digest("hex"), `length ${n}`);
  }
});

test("md5 of a 4 MiB image matches node:crypto", () => {
  const data = new Uint8Array(randomBytes(4 * 1024 * 1024));
  assert.equal(toHex(md5(data)), createHash("md5").update(data).digest("hex"));
});

test("md5 works on subarray views", () => {
  const data = new Uint8Array(randomBytes(300));
  const view = data.subarray(10, 200);
  assert.equal(toHex(md5(view)), createHash("md5").update(view).digest("hex"));
});

test("toHex pads and lowercases", () => {
  assert.equal(toHex([0, 1, 15, 16, 255]), "00010f10ff");
});

test("sha256Hex matches node:crypto", async () => {
  const data = new Uint8Array(randomBytes(1234));
  assert.equal(await sha256Hex(data), createHash("sha256").update(data).digest("hex"));
  assert.equal(await sha256Hex(new Uint8Array(0)),
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
});
