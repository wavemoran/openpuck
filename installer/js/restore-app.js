// Page wiring for restore.html. Same shape as app.js: check support, then load the flow
// dynamically, let the user pick a backup file, and run flow.runRestore. No protocol logic.
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
let backup = null;

const handlers = {
  cancel() { controller.abort(); bridge.cancel(); },
  retry() { return last?.resume ? finished(last.resume()) : run(); },
};
const view = createView({ mode: "restore", refs, cues, bridge, handlers, setBusy: guardUnload() });
wireLog({ logBox: refs.logBox, copyButton: $("copy") });

// Start needs a valid backup file and the risk checkbox.
function updateStart() { $("start").disabled = !(backup && $("ack").checked); }
async function finished(promise) { last = await promise; }
function run() {
  controller = new AbortController();
  return finished(flow.runRestore(backup, { ui: view, deps, signal: controller.signal }));
}

// Read the chosen file and show any problems right away.
async function onFile(event, checkBackup) {
  const file = event.target.files[0];
  const problems = $("problems");
  backup = null;
  updateStart();
  problems.replaceChildren();
  if (!file) return;
  const bytes = new Uint8Array(await file.arrayBuffer());
  const check = checkBackup(bytes);
  if (check.ok) {
    backup = bytes;
    updateStart();
    problems.append(el("p", { class: "ok" }, `${file.name} looks like a full backup.`));
  } else {
    problems.append(el("p", { class: "alert" }, "This file isn't a full backup of the puck:"),
      el("ul", {}, check.problems.map((p) => el("li", {}, p))));
  }
}

async function main() {
  const support = checkSupport();
  if (!support.ok) {
    view.log(`unsupported: ${support.reason}`);
    view.showMessage("This browser can't restore your puck", [
      el("p", {}, support.message),
      el("p", {}, "You can also use the ", el("a", { href: `${LINKS.install}#going-back-to-the-original-software` },
        "command-line instructions"), "."),
    ]);
    return;
  }
  let checkBackup;
  try {
    flow = await import("./flow.js");
    ({ checkBackup } = await import("./images.js"));
  } catch (e) {
    view.showMessage("The page didn't load", [el("p", {}, `Please reload the page. (${e.message})`)]);
    return;
  }
  deps = flow.defaultDeps({ serial: navigator.serial, requestPort: bridge.requestPort });
  $("intro").hidden = false;
  $("steps").hidden = false;
  $("ack").addEventListener("change", updateStart);
  $("file").addEventListener("change", (e) => onFile(e, checkBackup));
  $("start").addEventListener("click", () => { cues.unlock(); run(); });
}
main();
