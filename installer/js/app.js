// Page wiring for index.html (install). Checks browser support first, then loads the flow
// (and esptool-js with it) only for supported browsers. No protocol logic lives here.
import { checkSupport } from "./support.js";
import { USB_FILTERS } from "./constants.js";
import { LINKS, createCues, createPortBridge, createView, el, guardUnload, wireLog } from "./ui.js";

const $ = (id) => document.getElementById(id);
const refs = { main: $("main"), status: $("status"), logBox: $("log") };
const cues = createCues();
const bridge = createPortBridge(navigator.serial, USB_FILTERS);
let controller = new AbortController();
let last = null;
let flow = null;
let deps = null;

const handlers = {
  cancel() { controller.abort(); bridge.cancel(); },
  retry() { return last?.resume ? resume() : run(); },
};
const view = createView({ mode: "install", refs, cues, bridge, handlers, setBusy: guardUnload() });
wireLog({ logBox: refs.logBox, copyButton: $("copy") });

async function finished(promise) {
  last = await promise;
  // After a failed write without a resume, starting over would back up a half-written puck.
  if (!last.ok && last.writeStarted && !last.resume) view.hideRetry();
}
function run() {
  controller = new AbortController();
  return finished(flow.runInstall({ ui: view, deps, signal: controller.signal }));
}
function resume() { return finished(last.resume()); }

async function main() {
  const support = checkSupport();
  if (!support.ok) {
    view.log(`unsupported: ${support.reason}`);
    view.showMessage("This browser can't install OpenPuck", [
      el("p", {}, support.message),
      el("p", {}, "You can also use the ", el("a", { href: LINKS.install }, "command-line instructions"), "."),
    ]);
    return;
  }
  try {
    flow = await import("./flow.js"); // only supported browsers load esptool-js
  } catch (e) {
    view.showMessage("The installer didn't load", [
      el("p", {}, `Please reload the page. (${e.message})`),
      el("p", {}, "You can also use the ", el("a", { href: LINKS.install }, "command-line instructions"), "."),
    ]);
    return;
  }
  deps = flow.defaultDeps({ serial: navigator.serial, requestPort: bridge.requestPort });
  $("intro").hidden = false;
  $("steps").hidden = false;
  $("ack").addEventListener("change", () => { $("start").disabled = !$("ack").checked; });
  $("start").addEventListener("click", () => { cues.unlock(); run(); });
}
main();
