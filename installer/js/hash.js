// SHA-256 via Web Crypto (available in browsers and Node >= 20).
import { toHex } from "./md5.js";

/**
 * SHA-256 of bytes as lowercase hex.
 * @param {Uint8Array} bytes
 * @returns {Promise<string>}
 */
export async function sha256Hex(bytes) {
  const digest = await globalThis.crypto.subtle.digest("SHA-256", bytes);
  return toHex(new Uint8Array(digest));
}
