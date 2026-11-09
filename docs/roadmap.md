# Roadmap: distribution, testing and releases

## 1. Easy install for non-technical owners

**Goal:** someone with a Life Puck, a USB-C cable and a computer can back up
their puck and install OpenPuck in a few minutes, **without installing
anything**.

### Browser installer on GitHub Pages: built

A web page using Espressif's [esptool-js](https://github.com/espressif/esptool-js)
over the Web Serial API. Guided flow, one step per screen: connect, hold the
power button (only if the puck switched off on connect), back up (required,
downloaded before anything is written), install and verify, done. A separate
restore page writes a backup file back. Release builds publish the
bootloader, partition table and app plus a manifest, so the page always offers
the latest release.

How it works, supported browsers and how to run the tests are in
[`installer.md`](installer.md). Owners start at [`install.md`](install.md).

**Spike results:**

- esptool-js can drive the latch registers and read GPIO10 in the same session.
- After the puck re-enumerates, a granted port can be reopened without a user
  gesture through the `connect` event. `getPorts()` is empty on a first visit.
- esptool-js 0.7.0 `hard_reset` never raises RTS, and its `readFlash` is not
  usable, so the installer has its own reset and verified read.
- Chrome needs a 1 MiB buffer size when opening the port.
- Chrome on macOS corrupts runs of `0xFF` bytes while reading, so Macs need
  Firefox.

**Remaining:**

- Test on Windows and Linux hardware.
- Test the original-software path (power button hold) end to end on hardware.
- Chrome on macOS: would need a small helper uploaded to the puck that avoids
  sending raw `0xFF` runs.

### Fallback: `tools/flash.py`

For unsupported browsers and developers. Needs Python + esptool. Optionally
later: packaged single-file executables (e.g. PyInstaller) for macOS and
Windows, built in CI.

## 2. Testing and code coverage

**Architecture rule:** everything that isn't hardware lives in a **pure C
component with no ESP-IDF dependencies** (`components/puck_core`): life
totals, players, history, timer, settings, the save scheduler (decides *when*
to save) and the state serialization format. Hardware code (`main/`) stays a
thin layer. That makes most of the firmware testable on a normal computer.

| Layer | What | How | Runs |
|---|---|---|---|
| Unit | `puck_core` logic | Unity + CMake on the host (gcc/clang), sanitizers on | Every push |
| Coverage | `puck_core` | gcov + gcovr, report published, threshold enforced (target ≥ 90 % lines) | Every push |
| Serialization | Saved-state format | Round-trip and compatibility tests with stored fixtures from every released version | Every push |
| UI | Screens render without errors | LVGL on the host (headless), screenshot comparison | Every push (later) |
| Firmware build | Full ESP-IDF build for esp32c3, size report, warnings as errors | `espressif/idf:v6.1` container | Every push |
| Installer | Web installer logic | JS unit tests with a mocked esptool-js/serial port | Every push  |
| Hardware-in-the-loop | Flash a real puck, boot, check the log | Self-hosted runner with a puck attached | Optional, before releases |

## 3. GitHub Actions

- **`ci.yml`** (push / PR): host unit tests + coverage gate, ESP-IDF build,
  formatting and static checks, artifacts for the firmware binaries.
- **`release.yml`** (on a version tag `vX.Y.Z`): build, run all tests, attach
  firmware binaries + manifest + checksums to a GitHub Release, and deploy the
  web installer (with that release) to GitHub Pages.
- Versioning: semantic versions; `PROJECT_VER` comes from the git tag.

## Before the first public release

- Choose a license.
- Web installer tested by someone non-technical on Windows and macOS.
- Restore path tested end to end on a real device.
