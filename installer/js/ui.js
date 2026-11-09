// Shared screen rendering for the install and restore pages: DOM only, no protocol logic.
// createView() builds the `ui` object the flow calls (show, progress, cue, saveBackup,
// confirmBackupSaved). createCues() plays tones and speech. createPortBridge() connects the
// Connect button's click to the promise the flow awaits. guardUnload() warns before leaving.

export const LINKS = {
  repo: "https://github.com/wavemoran/openpuck",
  install: "https://github.com/wavemoran/openpuck/blob/main/docs/install.md",
  guide: "https://github.com/wavemoran/openpuck/blob/main/docs/user-guide.md",
  trouble: "https://github.com/wavemoran/openpuck/blob/main/docs/troubleshooting.md",
};

/** Create an element. Props: class, on<event> handlers, true/false attributes. Kids: nodes or text. */
export function el(tag, props = {}, ...kids) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (key === "class") node.className = value;
    else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
    else if (value === true) node.setAttribute(key, "");
    else if (value !== false && value != null) node.setAttribute(key, value);
  }
  node.append(...kids.flat().filter((k) => k != null));
  return node;
}

// Which step-indicator item each screen belongs to (hold and release belong to Connect).
const STAGE = {
  install: { firmware: 0, connect: 0, connecting: 0, hold: 0, release: 0, prepare: 0, backup: 1, "save-backup": 1, writing: 2, finishing: 2, done: 3 },
  restore: { firmware: 1, connect: 1, connecting: 1, hold: 1, release: 1, prepare: 1, writing: 2, finishing: 2, done: 3 },
};

const link = (href, text) => el("a", { href, target: "_blank", rel: "noopener" }, text);
const para = (...kids) => el("p", {}, ...kids);

/** Turn a fixed SVG string (never user data) into an element. */
function svg(markup) {
  const wrap = el("div", { class: "visual", "aria-hidden": "true" });
  wrap.innerHTML = markup;
  return wrap;
}

// A round puck seen from above with its power button on the right edge.
const PUCK_BODY = `<circle class="body" cx="100" cy="100" r="78"/><circle class="face" cx="100" cy="100" r="56"/>`;
const HOLD_SVG = `<svg viewBox="0 0 200 200" role="img" focusable="false">${PUCK_BODY}
  <circle class="ring pulse" cx="176" cy="100" r="14"/><rect class="btn-pressed" x="172" y="90" width="8" height="20" rx="3"/>
  <path class="arrow" d="M206 100 L190 100 M198 92 L190 100 L198 108"/></svg>`;
const RELEASE_SVG = `<svg viewBox="0 0 200 200" role="img" focusable="false">${PUCK_BODY}
  <rect class="btn-up" x="174" y="90" width="8" height="20" rx="3"/>
  <path class="arrow" d="M178 70 L178 50 M170 58 L178 50 L186 58 M178 130 L178 150 M170 142 L178 150 L186 142"/></svg>`;

/** Format a byte count for display. */
const fmtMB = (bytes) => `${(bytes / 1048576).toFixed(1)} MB`;

/**
 * Warn before the page is closed or reloaded while busy.
 * @returns {(busy: boolean) => void}
 */
export function guardUnload() {
  const handler = (e) => { e.preventDefault(); e.returnValue = ""; };
  let on = false;
  return (busy) => {
    if (busy && !on) window.addEventListener("beforeunload", handler);
    if (!busy && on) window.removeEventListener("beforeunload", handler);
    on = busy;
  };
}

/**
 * Sound and speech cues. Muted until the person turns sound on (kept for this visit only).
 * Only the hold and release cues make sound. Call unlock() from a click so the browser allows audio.
 */
export function createCues() {
  let ctx = null;
  let on = false;
  const listeners = new Set();
  const TONES = { // [frequency Hz, start s, length s, peak gain]
    hold: [[660, 0, 0.18, 0.25], [660, 0.28, 0.18, 0.25]],
    release: [[1040, 0, 0.12, 0.25], [1040, 0.18, 0.12, 0.25], [1040, 0.36, 0.12, 0.25]],
    confirm: [[880, 0, 0.12, 0.08]], // very short and soft: "your volume works"
  };
  const SPEECH = { hold: "Press and hold the power button", release: "Release the button now" };

  function unlock() {
    try {
      ctx ||= new (window.AudioContext || window.webkitAudioContext)();
      ctx.resume();
    } catch (_) { /* no audio */ }
  }
  function tones(name) {
    if (!ctx) return;
    for (const [freq, start, len, peak] of TONES[name] ?? []) {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      const t = ctx.currentTime + start;
      osc.type = "sine";
      osc.frequency.value = freq;
      gain.gain.setValueAtTime(0.0001, t);
      gain.gain.exponentialRampToValueAtTime(peak, t + 0.02);
      gain.gain.exponentialRampToValueAtTime(0.0001, t + len);
      osc.connect(gain).connect(ctx.destination);
      osc.start(t);
      osc.stop(t + len + 0.05);
    }
  }
  function say(name) {
    if (!SPEECH[name] || !("speechSynthesis" in window)) return;
    speechSynthesis.cancel();
    const u = new SpeechSynthesisUtterance(SPEECH[name]);
    u.lang = "en-US";
    speechSynthesis.speak(u);
  }

  return {
    unlock,
    isOn: () => on,
    /** Turn sound on or off. Call from a click: turning on unlocks audio and plays a soft tone. */
    setOn(value) {
      on = Boolean(value);
      if (on) { unlock(); try { tones("confirm"); } catch (_) { /* optional */ } }
      else try { speechSynthesis.cancel(); } catch (_) { /* no speech */ }
      for (const fn of [...listeners]) fn(on);
    },
    /** Call fn(on) whenever the setting changes. */
    subscribe(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    /** Speak a cue's instruction right now (used when unmuting on the hold screen). */
    say(name) { if (on) try { say(name); } catch (_) { /* optional */ } },
    play(name) {
      if (!on) return;
      try { tones(name); say(name); } catch (_) { /* cues are optional */ }
    },
  };
}

/**
 * Bridge between the Connect button and the flow's `requestPort` dependency.
 * The flow awaits requestPort(); the button's click handler calls pick(), which opens the
 * browser's picker synchronously inside the click (a user gesture is required).
 */
export function createPortBridge(serial, filters) {
  let waiting = null;
  let picking = false;
  return {
    /** The flow's deps.requestPort: resolves when the user has chosen a puck. */
    requestPort() {
      return new Promise((resolve, reject) => { waiting = { resolve, reject }; });
    },
    /** Call directly from the click handler. Resolves "chosen", "canceled" or "busy". */
    async pick() {
      const w = waiting;
      if (!w || picking) return "busy";
      picking = true;
      try {
        const port = await serial.requestPort({ filters }); // called synchronously in the click
        waiting = null;
        w.resolve(port);
        return "chosen";
      } catch (e) {
        if (e && e.name === "NotFoundError") return "canceled"; // closed the picker: stay and retry
        waiting = null;
        w.reject(e);
        return "chosen"; // the flow now shows the error
      } finally {
        picking = false;
      }
    },
    /** Stop waiting for a choice (Cancel button). */
    cancel() {
      if (!waiting) return;
      const e = new Error("Canceled");
      e.name = "AbortError";
      waiting.reject(e);
      waiting = null;
    },
  };
}

/**
 * Build the view and the flow's `ui` object.
 * @param {object} o
 * @param {"install"|"restore"} o.mode
 * @param {{main: Element, status: Element, logBox: Element}} o.refs page elements
 * @param {ReturnType<typeof createCues>} o.cues
 * @param {ReturnType<typeof createPortBridge>} o.bridge
 * @param {{cancel: Function, retry: Function}} o.handlers
 * @param {(busy: boolean) => void} o.setBusy
 */
export function createView({ mode, refs, cues, bridge, handlers, setBusy }) {
  const { main, status, logBox } = refs;
  const intro = main.querySelector("#intro");
  const screen = main.querySelector("#screen");
  const install = mode === "install";
  let bar = null;
  let lastTenth = -1;
  let lastBackup = null;
  let confirm = null;
  let saved = null;
  let retryButton = null;

  function log(line) {
    // Local time, matching the time in the backup file name.
    logBox.textContent += `${new Date().toTimeString().slice(0, 8)}  ${line}\n`;
  }

  const SPEAKER = (waves) => `<svg viewBox="0 0 24 24" width="18" height="18" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 9v6h4l5 4V5L8 9z" fill="currentColor"/>${waves ? '<path d="M16.5 8.5a5 5 0 0 1 0 7M19 6a8.5 8.5 0 0 1 0 12"/>' : '<path d="M17 9l5 6M22 9l-5 6"/>'}</svg>`;
  const icon = (waves) => { const i = el("span", { class: "icon", "aria-hidden": "true" }); i.innerHTML = SPEAKER(waves); return i; };

  /** A button that turns sound on and off. `labels` = [text when off, text when on]. */
  function soundButton(labels, cls) {
    const button = el("button", { type: "button", class: cls });
    const paint = (on) => {
      button.replaceChildren(icon(on), el("span", {}, labels[on ? 1 : 0]));
      button.setAttribute("aria-pressed", String(on));
    };
    button.addEventListener("click", () => {
      const turningOn = !cues.isOn();
      cues.setOn(turningOn);
      if (turningOn && button.closest(".step-hold")) cues.say("hold"); // unmuted mid-hold: say it once now
    });
    const off = cues.subscribe((on) => (button.isConnected ? paint(on) : off()));
    paint(cues.isOn());
    return button;
  }

  // The note near Start on the intro screen.
  const soundNote = main.querySelector("#sound-note");
  if (soundNote) {
    const text = el("span", {});
    const paint = (on) => {
      text.textContent = on
        ? "Sound is on. The page will tell you out loud when to hold the power button and when to let go."
        : "Sound is off. If your puck still has its original software, you'll hold its power button while looking at the puck, and the page can tell you out loud when to let go. Turn on sound if you'd like that.";
    };
    cues.subscribe(paint);
    paint(cues.isOn());
    soundNote.replaceChildren(el("p", { class: "note sound-note" }, text),
      soundButton(["Turn on sound", "Turn off sound"], "btn secondary small"));
  }

  const steps = main.querySelector("#steps");
  function setStage(id) {
    const n = STAGE[mode][id];
    if (!steps || n === undefined) return; // keep the current step (e.g. on an error screen)
    steps.querySelectorAll("li").forEach((li, i) => {
      li.classList.toggle("done", i < n);
      if (i === n) li.setAttribute("aria-current", "step"); else li.removeAttribute("aria-current");
    });
  }

  function render(title, nodes, { cancel = false, id = "" } = {}) {
    const heading = el("h2", { tabindex: "-1" }, title);
    const kids = [heading, ...nodes];
    if (cancel) {
      kids.push(el("div", { class: "actions" },
        el("button", { type: "button", class: "quiet", onclick: handlers.cancel }, "Cancel")));
    }
    intro.hidden = true;
    screen.hidden = false;
    if (id === "message") { // unsupported or load failure: no steps
      if (steps) steps.hidden = true;
    } else setStage(id);
    screen.replaceChildren(el("section", { class: `step step-${id}` }, ...kids));
    status.textContent = title;
    heading.focus();
    window.scrollTo(0, 0);
  }

  function progressBlock() {
    const fill = el("div", { class: "fill" });
    const label = el("p", { class: "progress-label" });
    const pct = el("span", { class: "pct" }, "0%");
    const track = el("div", {
      class: "track", role: "progressbar", "aria-label": "Progress",
      "aria-valuemin": "0", "aria-valuemax": "100", "aria-valuenow": "0",
    }, fill);
    bar = { fill, label, pct, track };
    lastTenth = -1;
    return el("div", { class: "progress" }, track, el("p", { class: "progress-row" }, label, pct));
  }

  function connectScreen(d) {
    const msg = el("p", { class: "note", role: "status" });
    const button = el("button", { type: "button", class: "btn primary big" }, "Connect");
    button.addEventListener("click", () => {
      cues.unlock();
      button.disabled = true;
      // pick() starts requestPort synchronously, so the click's user gesture is still valid.
      bridge.pick().then((outcome) => {
        button.disabled = false;
        msg.textContent = outcome === "canceled"
          ? "No puck was chosen. Click Connect and pick the puck from the list (USB JTAG/serial debug unit)." : "";
      });
    });
    render("Connect your puck", [
      para("Plug the puck into this computer and turn it on. Then click Connect and choose the puck ",
        el("strong", {}, "(USB JTAG/serial debug unit)"), "."),
      para("If the puck isn't in the list, try a different USB-C cable. Some cables only charge."),
      d.version ? el("p", { class: "note" }, `OpenPuck version ${d.version} is ready to install.`) : null,
      el("div", { class: "actions" }, button), msg,
    ], { cancel: true, id: "connect" });
  }

  function saveScreen(d) {
    const again = el("button", { type: "button", class: "quiet" }, "Download again");
    again.addEventListener("click", () => lastBackup && download(lastBackup.name, lastBackup.bytes));
    const check = el("input", { type: "checkbox", id: "saved" });
    const go = el("button", { type: "button", class: "btn primary big", disabled: true }, "Continue");
    check.addEventListener("change", () => { go.disabled = !check.checked; });
    go.addEventListener("click", () => { go.disabled = true; check.disabled = true; confirm?.(); });
    render("Save your backup", [
      para("Your backup is downloading. ", el("strong", {}, "Keep this file somewhere safe and private."),
        " It is the only way to go back to the original software. Don't share it or post it online."),
      el("dl", { class: "facts" },
        el("dt", {}, "File name"), el("dd", {}, d.fileName),
        el("dt", {}, "SHA-256"), el("dd", { class: "hash" }, d.sha256)),
      para("Check your Downloads folder, then copy the file to a cloud drive or a USB stick."),
      el("div", { class: "actions" }, again),
      el("p", {}, el("label", { class: "check" }, check, " I have saved my backup file")),
      el("div", { class: "actions" }, go),
    ], { id: "save-backup" });
  }

  function doneScreen(d) {
    if (d.kind === "restore") {
      render("Your backup is restored", [
        para(d.hint),
        para("The puck is back the way it was when you made the backup."),
      ], { id: "done" });
      return;
    }
    render("OpenPuck is installed.", [
      para("OpenPuck starts by itself. If the screen stays dark, press the power button."),
      para(el("strong", {}, "Keep your backup file"), d.fileName ? ` (${d.fileName})` : "",
        ". Store it somewhere safe and private. You need it to go back to the original software."),
      para("You can unplug the puck now."),
      el("div", { class: "actions" },
        el("a", { class: "btn primary big", href: LINKS.guide, target: "_blank", rel: "noopener" }, "Read the user guide"),
        el("a", { class: "btn secondary", href: LINKS.repo, target: "_blank", rel: "noopener" }, "View on GitHub")),
    ], { id: "done" });
  }

  function errorScreen(d) {
    retryButton = el("button", { type: "button", class: "btn primary big", onclick: handlers.retry }, "Try again");
    const kids = [];
    if (d.dontUnplug) kids.push(el("p", { class: "alert" }, "Don't unplug the puck."));
    kids.push(para(d.message));
    if (d.dontUnplug) {
      kids.push(para(install
        ? "The puck may not work right now. Try again, or put the original software back with your backup file."
        : "The restore didn't finish. Keep the puck plugged in and try again."));
    }
    if (d.pointToRestore && install) {
      kids.push(para(link("restore.html", "Restore your backup"), " to put the original software back."));
    }
    kids.push(para("Need help? See ", link(LINKS.trouble, "troubleshooting"), "."));
    kids.push(el("div", { class: "actions" }, retryButton));
    render(d.aborted ? "Canceled" : "Something went wrong", kids, { id: "error" });
  }

  const SCREENS = {
    firmware: () => render("Getting ready", [para("Checking the OpenPuck files. This takes a few seconds.")], { id: "firmware" }),
    connect: connectScreen,
    connecting: () => render("Connecting", [para("Talking to the puck. This takes a few seconds.")], { id: "connecting" }),
    hold: () => render("Press and hold the power button. Keep holding it.", [
      svg(HOLD_SVG),
      el("p", { class: "sound-row" }, soundButton(["Sound off", "Sound on"], "quiet sound-toggle")),
      para("The puck switched off when it connected. That is normal for the original software."),
      para("Hold the button until this page tells you to let go."),
    ], { cancel: true, id: "hold" }),
    release: () => render("Release the button now", [
      svg(RELEASE_SVG),
      el("p", { class: "sound-row" }, soundButton(["Sound off", "Sound on"], "quiet sound-toggle")),
      para("The puck is connected. Let go of the power button."),
    ], { cancel: true, id: "release" }),
    prepare: () => render("Checking the puck", [para("Making sure this is the right kind of puck.")], { id: "prepare" }),
    backup: () => render("Backing up your puck", [
      para("This saves a copy of everything on the puck. It takes about a minute. Keep the puck plugged in."),
      progressBlock(),
    ], { cancel: true, id: "backup" }),
    "save-backup": saveScreen,
    writing: () => render(install ? "Installing OpenPuck" : "Restoring your backup", [
      el("p", { class: "alert" }, "Don't unplug the puck."),
      para("Writing and checking the files. This takes a minute or two."),
      progressBlock(),
    ], { id: "writing" }),
    finishing: () => render("Finishing up", [
      el("p", { class: "alert" }, "Don't unplug the puck yet."), para("Restarting the puck.")], { id: "finishing" }),
    done: doneScreen,
    error: errorScreen,
  };

  const BUSY = new Set(["backup", "save-backup", "writing", "finishing"]);

  function download(name, bytes) {
    const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
    const a = el("a", { href: url, download: name, hidden: true });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }

  return {
    log,
    /** Show a standalone screen (used for "unsupported" and load failures). */
    showMessage(title, nodes) { render(title, nodes, { id: "message" }); },
    /** Hide the Try again button (used when starting over would be unsafe). */
    hideRetry() { retryButton?.remove(); },
    // The flow's ui interface.
    show(step, details = {}) {
      log(`step: ${step}${details.message ? ` (${details.message})` : ""}`);
      setBusy(BUSY.has(step));
      bar = null;
      if (step === "save-backup") saved = new Promise((resolve) => { confirm = resolve; });
      (SCREENS[step] ?? (() => render(step, [], { id: step })))(details);
    },
    progress(done, total, label) {
      if (!bar || !total) return;
      const pct = Math.min(100, Math.floor((done / total) * 100));
      bar.fill.style.width = `${pct}%`;
      bar.track.setAttribute("aria-valuenow", String(pct));
      bar.pct.textContent = `${pct}%`;
      bar.label.textContent = `${label ?? ""} (${fmtMB(done)} of ${fmtMB(total)})`;
      const tenth = Math.floor(pct / 10);
      if (tenth !== lastTenth) { // announce every 10% only
        lastTenth = tenth;
        status.textContent = `${label ?? "Working"}: ${pct}%`;
        log(`progress: ${pct}%`);
      }
    },
    cue(name) { log(`cue: ${name}`); cues.play(name); },
    saveBackup(fileName, bytes, sha256) {
      lastBackup = { name: fileName, bytes };
      log(`backup ${fileName} sha256 ${sha256}`);
      download(fileName, bytes);
    },
    confirmBackupSaved() { return saved; },
  };
}

/** Wire the footer-style "Details" log: Copy button. */
export function wireLog({ logBox, copyButton }) {
  copyButton.addEventListener("click", async () => {
    const text = logBox.textContent;
    try {
      await navigator.clipboard.writeText(text);
      copyButton.textContent = "Copied";
    } catch (_) {
      const range = document.createRange(); // fall back to selecting the text
      range.selectNodeContents(logBox);
      getSelection().removeAllRanges();
      getSelection().addRange(range);
      copyButton.textContent = "Press Ctrl+C to copy";
    }
    setTimeout(() => { copyButton.textContent = "Copy"; }, 2500);
  });
}
