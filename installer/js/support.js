// Browser/OS support check for the installer.

const MOBILE_RE = /Android|iPhone|iPad|iPod/i;
const CHROMIUM_RE = /Chrome\/|Chromium\/|CriOS\/|Edg\/|EdgA\/|OPR\/|Vivaldi\/|Brave\//;

/**
 * Detect the environment from the global navigator (browser only).
 * @returns {object} input for checkSupport
 */
export function detectEnvironment() {
  const nav = globalThis.navigator ?? {};
  return {
    userAgent: nav.userAgent ?? "",
    userAgentData: nav.userAgentData,
    platform: nav.platform ?? "",
    maxTouchPoints: nav.maxTouchPoints ?? 0,
    hasSerial: !!nav.serial,
  };
}

/**
 * Decide whether this browser can run the installer.
 * @param {{userAgent?: string, userAgentData?: {mobile?: boolean, platform?: string, brands?: {brand: string}[]},
 *   platform?: string, maxTouchPoints?: number, hasSerial?: boolean}} [env] defaults to the current browser
 * @returns {{ok: boolean, reason: "ok"|"mobile"|"no-serial"|"mac-chromium", message: string}}
 */
export function checkSupport(env = detectEnvironment()) {
  const { userAgent = "", userAgentData, platform = "", maxTouchPoints = 0, hasSerial = false } = env;
  const isMac = /Mac/i.test(platform) || userAgentData?.platform === "macOS" || /Macintosh|Mac OS X/.test(userAgent);

  if (userAgentData?.mobile === true || MOBILE_RE.test(userAgent) || (isMac && maxTouchPoints > 1)) {
    return {
      ok: false,
      reason: "mobile",
      message: "Phones and tablets can't install OpenPuck. Please use a computer.",
    };
  }
  if (!hasSerial) {
    return {
      ok: false,
      reason: "no-serial",
      message: "This browser can't talk to USB devices. Please use Firefox 159 or newer, " +
        "or Chrome or Edge on Windows, Linux or ChromeOS.",
    };
  }
  const chromium = CHROMIUM_RE.test(userAgent) ||
    !!userAgentData?.brands?.some((b) => /Chromium/i.test(b.brand));
  if (isMac && chromium) {
    return {
      ok: false,
      reason: "mac-chromium",
      message: "On a Mac, please use Firefox. Chrome, Edge and other Chromium browsers on " +
        "macOS lose data while reading the puck, so they can't make a complete backup.",
    };
  }
  return { ok: true, reason: "ok", message: "" };
}
