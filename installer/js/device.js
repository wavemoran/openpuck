// Talking to the puck: connecting, power latch, flash preparation, reset.
import { ESPLoader } from "../vendor/esptool-js/bundle.js";
import { RobustTransport, sleep, withTimeout, abortError, throwIfAborted } from "./transport.js";
import {
  BAUD_RATE, BUTTON_PIN, BUTTON_POLL_MS, BUTTON_RELEASE_TIMEOUT_MS, DISCONNECT_TIMEOUT_MS,
  FUN_IE, GPIO_ENABLE_W1TS, GPIO_FUNC0_OUT_SEL, GPIO_IN, GPIO_OUT_W1TS, IO_MUX_GPIO0,
  LATCH_PIN, MCU_SEL_GPIO, MCU_SEL_MASK, OUT_SEL_SIMPLE_GPIO, RESET_PULSE_MS,
  SERIAL_BUFFER_SIZE, USB_PID, USB_VID,
} from "./constants.js";

/**
 * Open a serial port, reset the chip into the ROM loader and identify it.
 * On failure the port is closed again and the error is rethrown.
 * @param {object} port a Web Serial port
 * @param {{ESPLoader?: Function, Transport?: Function, tracing?: boolean}} [deps] injectable for tests
 * @returns {Promise<{loader: object, transport: object}>}
 */
export async function openLoader(port, deps = {}) {
  const Loader = deps.ESPLoader ?? ESPLoader;
  const TransportClass = deps.Transport ?? RobustTransport;
  const transport = new TransportClass(port, deps.tracing ?? false);
  const loader = new Loader({
    transport,
    baudrate: BAUD_RATE,
    romBaudrate: BAUD_RATE,
    serialOptions: { bufferSize: SERIAL_BUFFER_SIZE },
  });
  try {
    await loader.detectChip("default_reset");
  } catch (e) {
    await safeDisconnect(transport);
    throw e;
  }
  return { loader, transport };
}

async function setPinAsGpio(loader, pin, inputEnable) {
  const reg = IO_MUX_GPIO0 + 4 * pin;
  const value = (((await loader.readReg(reg)) & ~MCU_SEL_MASK) | MCU_SEL_GPIO) >>> 0;
  await loader.writeReg(reg, (inputEnable ? (value | FUN_IE) : value) >>> 0);
}

/**
 * Drive the power latch high from the ROM loader and enable reading the button
 * (same register sequence as tools/flash.py hold_power, without the wait).
 * @param {object} loader
 */
export async function holdPower(loader) {
  await setPinAsGpio(loader, LATCH_PIN, false);
  await loader.writeReg(GPIO_FUNC0_OUT_SEL + 4 * LATCH_PIN, OUT_SEL_SIMPLE_GPIO);
  await loader.writeReg(GPIO_OUT_W1TS, (1 << LATCH_PIN) >>> 0);
  await loader.writeReg(GPIO_ENABLE_W1TS, (1 << LATCH_PIN) >>> 0);
  await setPinAsGpio(loader, BUTTON_PIN, true);
}

/**
 * @param {object} loader
 * @returns {Promise<boolean>} true while the power button is pressed
 */
export async function isButtonHeld(loader) {
  return (((await loader.readReg(GPIO_IN)) >>> BUTTON_PIN) & 1) === 1;
}

/**
 * Poll until the power button is released.
 * @param {object} loader
 * @param {{timeoutMs?: number, pollMs?: number, signal?: AbortSignal,
 *   sleep?: (ms: number, signal?: AbortSignal) => Promise<void>, now?: () => number}} [options]
 */
export async function waitForButtonRelease(loader, options = {}) {
  const { timeoutMs = BUTTON_RELEASE_TIMEOUT_MS, pollMs = BUTTON_POLL_MS, signal } = options;
  const sleepFn = options.sleep ?? sleep;
  const now = options.now ?? Date.now;
  const end = now() + timeoutMs;
  while (await isButtonHeld(loader)) {
    throwIfAborted(signal);
    if (now() > end) {
      throw new Error(`The power button is still held after ${Math.round(timeoutMs / 1000)} seconds.`);
    }
    await sleepFn(pollMs, signal);
  }
}

/**
 * Upload the flasher stub, attach the flash and read its ID and the MAC address.
 * @param {object} loader
 * @returns {Promise<{flashId: number, flashSize: number, mac: string, chipName: string}>}
 */
export async function prepareFlash(loader) {
  const chip = await loader.runStub();
  await loader.flashSpiAttach(0);
  const flashId = await loader.readFlashId();
  const target = loader.chip ?? chip;
  const mac = await target.readMac(loader);
  return {
    flashId,
    flashSize: 1 << ((flashId >> 16) & 0xff),
    mac: String(mac),
    chipName: target.CHIP_NAME,
  };
}

/**
 * Reset the chip so it boots the installed app. esptool-js after("hard_reset")
 * never raises RTS, so pulse it ourselves.
 * @param {object} transport
 * @param {{sleep?: (ms: number) => Promise<void>, pulseMs?: number}} [options]
 */
export async function resetToApp(transport, options = {}) {
  const sleepFn = options.sleep ?? sleep;
  const pulse = options.pulseMs ?? RESET_PULSE_MS;
  await transport.setDTR(false);
  await transport.setRTS(true);
  await sleepFn(pulse);
  await transport.setRTS(false);
  await sleepFn(pulse);
}

/**
 * Wait for a previously granted puck (303a:1001) to appear again. The `connect`
 * listener exists only while waiting and is always removed.
 * @param {{serial: EventTarget, timeoutMs?: number, signal?: AbortSignal}} options
 * @returns {Promise<object>} the port from the connect event
 */
export function waitForPuck({ serial, timeoutMs = 600000, signal }) {
  return new Promise((resolve, reject) => {
    if (signal && signal.aborted) return reject(abortError());
    let timer;
    const cleanup = () => {
      serial.removeEventListener("connect", onConnect);
      clearTimeout(timer);
      if (signal) signal.removeEventListener("abort", onAbort);
    };
    const onConnect = (event) => {
      const port = event.target;
      let info = {};
      try { info = port.getInfo?.() ?? {}; } catch (_) { /* ignore */ }
      if (info.usbVendorId !== USB_VID || info.usbProductId !== USB_PID) return;
      cleanup();
      resolve(port);
    };
    const onAbort = () => { cleanup(); reject(abortError()); };
    serial.addEventListener("connect", onConnect);
    if (signal) signal.addEventListener("abort", onAbort, { once: true });
    timer = setTimeout(() => {
      cleanup();
      reject(new Error("The puck did not appear on USB."));
    }, timeoutMs);
  });
}

/**
 * Close the port, ignoring errors and never hanging.
 * @param {object} transport
 * @param {number} [timeoutMs]
 */
export async function safeDisconnect(transport, timeoutMs = DISCONNECT_TIMEOUT_MS) {
  try {
    await withTimeout(transport.disconnect(), timeoutMs, "disconnect");
  } catch (_) { /* already closed or unplugged */ }
}
