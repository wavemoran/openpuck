// A more robust Web Serial transport for esptool-js 0.7.0, plus small async helpers.
import { Transport } from "../vendor/esptool-js/bundle.js";

/**
 * Create the error used for cancelation.
 * @param {string} [message]
 * @returns {Error} with name "AbortError"
 */
export function abortError(message = "Canceled") {
  const e = new Error(message);
  e.name = "AbortError";
  return e;
}

/**
 * Throw an AbortError if the signal has fired.
 * @param {AbortSignal} [signal]
 */
export function throwIfAborted(signal) {
  if (signal && signal.aborted) throw abortError();
}

/**
 * Sleep that can be canceled.
 * @param {number} ms
 * @param {AbortSignal} [signal] rejects with AbortError when it fires
 * @returns {Promise<void>}
 */
export function sleep(ms, signal) {
  return new Promise((resolve, reject) => {
    if (signal && signal.aborted) return reject(abortError());
    let onAbort;
    const timer = setTimeout(() => {
      if (signal) signal.removeEventListener("abort", onAbort);
      resolve();
    }, ms);
    if (signal) {
      onAbort = () => { clearTimeout(timer); reject(abortError()); };
      signal.addEventListener("abort", onAbort, { once: true });
    }
  });
}

/**
 * Reject if a promise takes longer than ms.
 * @template T
 * @param {Promise<T>} promise
 * @param {number} ms
 * @param {string} [label]
 * @returns {Promise<T>}
 */
export function withTimeout(promise, ms, label = "operation") {
  let timer;
  const timeout = new Promise((_, reject) => {
    timer = setTimeout(() => reject(new Error(`${label} timed out after ${ms} ms`)), ms);
  });
  return Promise.race([Promise.resolve(promise), timeout]).finally(() => clearTimeout(timer));
}

const NON_FATAL = ["BufferOverrunError", "FramingError", "BreakError", "ParityError"];

/**
 * esptool-js Transport with a persistent reader, linear-time SLIP decoding,
 * cancelable reads and a count of recoverable serial errors.
 */
export class RobustTransport extends Transport {
  constructor(device, tracing = false, enableSlipReader = true) {
    super(device, tracing, enableSlipReader);
    /** Number of recoverable serial errors seen (break, framing, overrun, parity). */
    this.serialErrors = 0;
    /** Optional callback (name, message) for every serial error. */
    this.onSerialError = null;
  }

  /** Upstream logs every trace line to the console; only do so when tracing is on. */
  trace(message) {
    if (this.tracing) super.trace(message);
  }

  _noteError(error) {
    this.serialErrors++;
    try { this.onSerialError?.(error && error.name, error && error.message); } catch (_) { /* ignore */ }
  }

  /** @returns {"continue"|"break"} */
  _handleReadError(error) {
    this._noteError(error);
    if (error instanceof Error && NON_FATAL.includes(error.name)) {
      this.trace(`Recoverable serial port error: ${error.name}: ${error.message}`);
      return "continue";
    }
    if (typeof DOMException !== "undefined" && error instanceof DOMException) {
      if (this.onDeviceLostCallback) this.onDeviceLostCallback();
      else this.trace(`Unrecoverable serial port error: ${error.message}`);
      return "break";
    }
    this.trace(`Unrecoverable serial port error: ${error}`);
    return "break";
  }

  _append(value) {
    if (!value || !value.length) return;
    if (this.buffer.length === 0) this.buffer = Uint8Array.from(value);
    else this.buffer = this.appendArray(this.buffer, value);
  }

  /** Read loop: one reader per readable stream; a new one only after a recoverable error. */
  async readLoop() {
    while (this.device.readable) {
      this.reader = this.device.readable.getReader();
      let again = false;
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) {
            this.trace("Serial port done");
            break;
          }
          this._append(value);
        }
      } catch (error) {
        again = this._handleReadError(error) === "continue";
      } finally {
        try { this.reader.releaseLock(); } catch (_) { /* ignore */ }
      }
      if (!again) break;
    }
    this.trace("readLoop exited");
  }

  /**
   * Read one SLIP frame.
   * @param {number} timeout milliseconds to wait for data
   * @param {AbortSignal} [signal]
   * @returns {Promise<Uint8Array>} the frame without delimiters
   */
  async read(timeout, signal) {
    let packet = null; // growable buffer once a frame has started
    let plen = 0;
    let isEscaping = false;
    const push = (b) => {
      if (plen === packet.length) {
        const bigger = new Uint8Array(packet.length * 2);
        bigger.set(packet);
        packet = bigger;
      }
      packet[plen++] = b;
    };

    for (;;) {
      const start = Date.now();
      let bytes = new Uint8Array(0);
      while (Date.now() - start < timeout) {
        throwIfAborted(signal);
        if (this.buffer.length > 0) {
          bytes = this.buffer;
          this.buffer = new Uint8Array(0);
          break;
        }
        await sleep(1);
      }
      throwIfAborted(signal);

      if (bytes.length === 0) {
        throw new Error(packet === null
          ? "Serial data stream stopped: Possible serial noise or corruption."
          : "No serial data received.");
      }

      for (let i = 0; i < bytes.length; i++) {
        const byte = bytes[i];
        if (packet === null) {
          if (byte !== this.SLIP_END) {
            this.detectPanicHandler(bytes.subarray(0, 4096));
            throw new Error(`Invalid head of packet (0x${byte.toString(16)}): Possible serial noise or corruption.`);
          }
          packet = new Uint8Array(256);
          plen = 0;
        } else if (isEscaping) {
          isEscaping = false;
          if (byte === this.SLIP_ESC_END) push(this.SLIP_END);
          else if (byte === this.SLIP_ESC_ESC) push(this.SLIP_ESC);
          else {
            this.detectPanicHandler(bytes.subarray(0, 4096));
            throw new Error(`Invalid SLIP escape (0xdb, 0x${byte.toString(16)})`);
          }
        } else if (byte === this.SLIP_ESC) {
          isEscaping = true;
        } else if (byte === this.SLIP_END) {
          // Put any bytes after the delimiter back in front of whatever arrived meanwhile.
          if (i + 1 < bytes.length) {
            const rest = bytes.subarray(i + 1);
            this.buffer = this.buffer.length === 0 ? rest : this.appendArray(rest, this.buffer);
          }
          return packet.slice(0, plen);
        } else {
          push(byte);
        }
      }
    }
  }
}
