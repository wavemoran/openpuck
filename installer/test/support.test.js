import { test } from "node:test";
import assert from "node:assert/strict";
import { checkSupport, detectEnvironment } from "../js/support.js";

const UA = {
  winChrome: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  winEdge: "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0",
  linuxChrome: "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  macChrome: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36",
  macEdge: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 Edg/130.0.0.0",
  macOpera: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36 OPR/115.0.0.0",
  macFirefox: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:159.0) Gecko/20100101 Firefox/159.0",
  macSafari: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15",
  winFirefox: "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:159.0) Gecko/20100101 Firefox/159.0",
  android: "Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Mobile Safari/537.36",
  iphone: "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1",
  ipad: "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15",
};

const cases = [
  ["Windows Chrome", { userAgent: UA.winChrome, platform: "Win32", hasSerial: true }, true, "ok"],
  ["Windows Edge", { userAgent: UA.winEdge, platform: "Win32", hasSerial: true }, true, "ok"],
  ["Linux Chrome", { userAgent: UA.linuxChrome, platform: "Linux x86_64", hasSerial: true }, true, "ok"],
  ["Windows Firefox with serial", { userAgent: UA.winFirefox, platform: "Win32", hasSerial: true }, true, "ok"],
  ["Mac Firefox with serial", { userAgent: UA.macFirefox, platform: "MacIntel", hasSerial: true }, true, "ok"],
  ["Mac Chrome", { userAgent: UA.macChrome, platform: "MacIntel", hasSerial: true }, false, "mac-chromium"],
  ["Mac Edge", { userAgent: UA.macEdge, platform: "MacIntel", hasSerial: true }, false, "mac-chromium"],
  ["Mac Opera", { userAgent: UA.macOpera, platform: "MacIntel", hasSerial: true }, false, "mac-chromium"],
  ["Mac Chromium by client hints", {
    userAgent: "x", platform: "", hasSerial: true,
    userAgentData: { platform: "macOS", brands: [{ brand: "Chromium" }] },
  }, false, "mac-chromium"],
  ["Mac Safari", { userAgent: UA.macSafari, platform: "MacIntel", hasSerial: false }, false, "no-serial"],
  ["Windows Firefox without serial", { userAgent: UA.winFirefox, platform: "Win32", hasSerial: false }, false, "no-serial"],
  ["Android Chrome", { userAgent: UA.android, platform: "Linux armv81", hasSerial: true, maxTouchPoints: 5 }, false, "mobile"],
  ["iPhone", { userAgent: UA.iphone, platform: "iPhone", hasSerial: false, maxTouchPoints: 5 }, false, "mobile"],
  ["iPad as Mac", { userAgent: UA.ipad, platform: "MacIntel", hasSerial: false, maxTouchPoints: 5 }, false, "mobile"],
  ["mobile client hint", { userAgent: UA.winChrome, hasSerial: true, userAgentData: { mobile: true } }, false, "mobile"],
  ["Mac with one touch point is a Mac", { userAgent: UA.macFirefox, platform: "MacIntel", hasSerial: true, maxTouchPoints: 1 }, true, "ok"],
];

for (const [name, env, ok, reason] of cases) {
  test(`checkSupport: ${name}`, () => {
    const r = checkSupport(env);
    assert.equal(r.ok, ok);
    assert.equal(r.reason, reason);
    assert.equal(typeof r.message, "string");
    assert.equal(r.message === "", ok);
  });
}

test("messages tell the user what to do", () => {
  assert.match(checkSupport({ userAgent: UA.macChrome, platform: "MacIntel", hasSerial: true }).message, /please use Firefox/);
  assert.match(checkSupport({ userAgent: UA.iphone, platform: "iPhone" }).message, /computer/);
  assert.match(checkSupport({ userAgent: UA.macSafari, platform: "MacIntel" }).message, /Firefox 159/);
});

test("detectEnvironment reads navigator when present and survives its absence", () => {
  const saved = Object.getOwnPropertyDescriptor(globalThis, "navigator");
  try {
    Object.defineProperty(globalThis, "navigator", {
      value: { userAgent: "ua", platform: "p", maxTouchPoints: 2, serial: {} }, configurable: true,
    });
    assert.deepEqual(detectEnvironment(), {
      userAgent: "ua", userAgentData: undefined, platform: "p", maxTouchPoints: 2, hasSerial: true,
    });
    Object.defineProperty(globalThis, "navigator", { value: undefined, configurable: true });
    assert.equal(detectEnvironment().hasSerial, false);
    assert.equal(checkSupport().reason, "no-serial");
  } finally {
    if (saved) Object.defineProperty(globalThis, "navigator", saved);
    else delete globalThis.navigator;
  }
});
