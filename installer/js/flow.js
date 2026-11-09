// Install and restore flows. All I/O goes through the injected `deps` and `ui` objects,
// so every path can be driven by fakes in tests.
//
// ui interface:
//   ui.show(stepName, details)            steps: "firmware", "connect", "connecting", "hold", "release",
//                                         "prepare", "backup", "save-backup", "writing", "finishing",
//                                         "done", "error"
//   ui.progress(done, total, label)
//   ui.cue("hold" | "release" | "done" | "error")
//   ui.saveBackup(fileName, bytes, sha256Hex)
//   await ui.confirmBackupSaved()         resolves when the user confirms the backup is saved
//
// deps interface (see defaultDeps): fetchFirmware, requestPort, openLoader, holdPower,
//   isButtonHeld, waitForButtonRelease, waitForPuck, safeDisconnect, resetToApp, prepareFlash,
//   readFlashVerified, writeImagesVerified, sha256Hex, sleep.
import {
  BUTTON_RELEASE_TIMEOUT_MS, BUTTON_SETTLE_MS, EXPECTED_FLASH_ID, FLASH_SIZE,
  PUCK_SETTLE_MS, PUCK_WAIT_TIMEOUT_MS,
} from "./constants.js";
import {
  holdPower, isButtonHeld, openLoader, prepareFlash, resetToApp, safeDisconnect,
  waitForButtonRelease, waitForPuck,
} from "./device.js";
import { readFlashVerified, writeImagesVerified } from "./flashio.js";
import { sha256Hex } from "./hash.js";
import { backupFileName, checkBackup, fetchFirmware, planInstall } from "./images.js";
import { sleep } from "./transport.js";

/**
 * Real dependencies for use in a browser.
 * @param {{serial: object, requestPort: () => Promise<object>, fetch?: Function, baseUrl?: string}} options
 *   `requestPort` is supplied by the UI (navigator.serial.requestPort with the 303a:1001 filter,
 *   called from the Connect click).
 * @returns {object} deps
 */
export function defaultDeps({ serial, requestPort, fetch = globalThis.fetch?.bind(globalThis), baseUrl = "firmware/" }) {
  return {
    fetchFirmware: () => fetchFirmware(fetch, baseUrl),
    requestPort,
    openLoader,
    holdPower,
    isButtonHeld,
    waitForButtonRelease,
    waitForPuck: ({ timeoutMs, signal }) => waitForPuck({ serial, timeoutMs, signal }),
    safeDisconnect,
    resetToApp,
    prepareFlash,
    readFlashVerified,
    writeImagesVerified,
    sha256Hex,
    sleep,
  };
}

/**
 * Connect to the puck and take over the power latch.
 * @param {object} port a Web Serial port the user granted
 * @param {{ui: object, deps: object, signal?: AbortSignal, session?: {conn: object|null}}} options
 *   `session.conn` is set as soon as a loader is open, so callers can clean up after a failure.
 * @returns {Promise<{runningOpenPuck: boolean, loader: object, transport: object}>}
 */
export async function connectPuck(port, { ui, deps, signal, session = { conn: null } }) {
  ui.show("connecting", {});
  let first = null;
  try {
    first = await deps.openLoader(port);
  } catch (_) {
    first = null; // the puck switched off during the reset (the original software does this)
  }
  if (first) {
    session.conn = first;
    await deps.holdPower(first.loader);
    if (await deps.isButtonHeld(first.loader)) {
      ui.show("release", {});
      ui.cue("release");
      await deps.waitForButtonRelease(first.loader, { timeoutMs: BUTTON_RELEASE_TIMEOUT_MS, signal });
    }
    return { runningOpenPuck: true, ...first };
  }

  ui.show("hold", {});
  ui.cue("hold");
  const newPort = await deps.waitForPuck({ timeoutMs: PUCK_WAIT_TIMEOUT_MS, signal });
  await deps.sleep(PUCK_SETTLE_MS, signal);
  const second = await deps.openLoader(newPort);
  session.conn = second;
  await deps.holdPower(second.loader);
  ui.show("release", {});
  ui.cue("release");
  await deps.waitForButtonRelease(second.loader, { timeoutMs: BUTTON_RELEASE_TIMEOUT_MS, signal });
  await deps.sleep(BUTTON_SETTLE_MS, signal);
  return { runningOpenPuck: false, ...second };
}

function assertFlashId(info) {
  if (info.flashId !== EXPECTED_FLASH_ID) {
    throw new Error(
      `This puck has an unexpected flash chip (ID 0x${(info.flashId >>> 0).toString(16)}, expected ` +
      `0x${EXPECTED_FLASH_ID.toString(16)}). Nothing was written.`);
  }
}

// Write the plan steps from session.nextStep on, tracking progress across steps.
async function executeSteps({ steps, session, ui, deps, signal }) {
  const total = steps.reduce((n, s) => n + s.data.length, 0);
  let base = steps.slice(0, session.nextStep).reduce((n, s) => n + s.data.length, 0);
  ui.show("writing", { total });
  while (session.nextStep < steps.length) {
    const step = steps[session.nextStep];
    session.writeStarted = true;
    await deps.writeImagesVerified(session.conn.loader, [{ address: step.address, data: step.data }], {
      signal,
      onProgress: (done, _t, label) => ui.progress(base + done, total, step.label ?? label),
    });
    base += step.data.length;
    session.nextStep++;
    ui.progress(base, total, step.label);
  }
}

async function finishUp({ session, ui, deps, details }) {
  ui.show("finishing", {});
  await deps.resetToApp(session.conn.transport);
  await deps.safeDisconnect(session.conn.transport);
  session.conn = null;
  ui.cue("done");
  ui.show("done", details);
}

// Report a failure. Before any write the puck is reset so it returns to normal.
async function fail(error, { session, ui, deps, resume }) {
  const writeStarted = session.writeStarted;
  const connected = session.conn;
  if (connected && !writeStarted) {
    try { await deps.resetToApp(connected.transport); } catch (_) { /* best effort */ }
    await deps.safeDisconnect(connected.transport);
    session.conn = null;
  }
  const message = error && error.message ? error.message : String(error);
  ui.cue("error");
  ui.show("error", {
    message,
    aborted: !!error && error.name === "AbortError",
    writeStarted,
    dontUnplug: writeStarted,
    pointToRestore: writeStarted,
    canRetry: true,
  });
  return {
    ok: false,
    error,
    writeStarted,
    resume: writeStarted && session.conn && resume ? resume : null,
  };
}

/**
 * The full install: verify firmware, connect, back up, write, verify, reset.
 * Nothing is written before the backup is verified, checked and the user has
 * confirmed it is saved. Never throws; resolves with a result object.
 * @param {{ui: object, deps: object, signal?: AbortSignal, now?: () => Date}} options
 * @returns {Promise<{ok: true, fileName: string, backupSha256: string} |
 *   {ok: false, error: Error, writeStarted: boolean, resume: (null|(() => Promise<object>))}>}
 */
export async function runInstall({ ui, deps, signal, now = () => new Date() }) {
  const session = { conn: null, writeStarted: false, nextStep: 0 };
  let steps = null;
  let doneInfo = null;

  const rest = async () => {
    try {
      await executeSteps({ steps, session, ui, deps, signal });
      await finishUp({ session, ui, deps, details: { kind: "install", ...doneInfo, startsByItself: true } });
      return { ok: true, ...doneInfo };
    } catch (e) {
      return fail(e, { session, ui, deps, resume: rest });
    }
  };

  try {
    ui.show("firmware", {});
    const firmware = await deps.fetchFirmware();
    ui.show("connect", { version: firmware.manifest.version });
    const port = await deps.requestPort();
    await connectPuck(port, { ui, deps, signal, session });

    ui.show("prepare", {});
    const info = await deps.prepareFlash(session.conn.loader);
    assertFlashId(info);

    ui.show("backup", {});
    const backup = await deps.readFlashVerified(session.conn.loader, 0, FLASH_SIZE, {
      signal,
      onProgress: (done, total) => ui.progress(done, total, "Reading your backup"),
    });
    const check = checkBackup(backup);
    if (!check.ok) {
      throw new Error(`The backup doesn't look right (${check.problems.join("; ")}). Nothing was written.`);
    }

    const fileName = backupFileName(info.mac, now());
    const backupSha256 = await deps.sha256Hex(backup);
    ui.show("save-backup", { fileName, sha256: backupSha256 });
    await ui.saveBackup(fileName, backup, backupSha256);
    await ui.confirmBackupSaved();
    // Only after the line above may anything be written or erased.

    const plan = planInstall({ backup, parts: firmware.parts });
    steps = plan.steps;
    doneInfo = { fileName, backupSha256, version: firmware.manifest.version, layoutChanged: plan.layoutChanged };
    return await rest();
  } catch (e) {
    return fail(e, { session, ui, deps, resume: null });
  }
}

/**
 * Restore a full 4 MiB backup file to the puck. Never throws.
 * @param {Uint8Array} fileBytes
 * @param {{ui: object, deps: object, signal?: AbortSignal}} options
 * @returns {Promise<{ok: true} | {ok: false, error: Error, writeStarted: boolean, resume: (null|(() => Promise<object>))}>}
 */
export async function runRestore(fileBytes, { ui, deps, signal }) {
  const session = { conn: null, writeStarted: false, nextStep: 0 };
  const steps = [{ kind: "write", label: "Restoring your backup", address: 0, data: fileBytes }];

  const rest = async () => {
    try {
      await executeSteps({ steps, session, ui, deps, signal });
      await finishUp({
        session, ui, deps,
        details: { kind: "restore", hint: "If the puck stays off, press the power button." },
      });
      return { ok: true };
    } catch (e) {
      return fail(e, { session, ui, deps, resume: rest });
    }
  };

  try {
    const check = checkBackup(fileBytes);
    if (!check.ok) {
      throw new Error(`This file isn't a full backup of the puck (${check.problems.join("; ")}). Nothing was written.`);
    }
    ui.show("connect", {});
    const port = await deps.requestPort();
    await connectPuck(port, { ui, deps, signal, session });
    ui.show("prepare", {});
    assertFlashId(await deps.prepareFlash(session.conn.loader));
    return await rest();
  } catch (e) {
    return fail(e, { session, ui, deps, resume: null });
  }
}
