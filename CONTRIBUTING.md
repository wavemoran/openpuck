# Contributing to OpenPuck

Thanks for helping. Bug reports, ideas, docs fixes and code are all welcome.
Please read the [Code of Conduct](CODE_OF_CONDUCT.md) first.

## Ground rule

Contributions must be your own work. **Do not include code, binaries, images
or other assets taken from the device's factory firmware.** OpenPuck is written
from scratch and has to stay that way. Please do not attach a flash backup
(it contains the factory firmware) to issues or pull requests either.

## Dev setup

1. Install [ESP-IDF](https://github.com/espressif/esp-idf) v6.1. The VS Code
   ESP-IDF extension's installation manager (EIM) works, as does Espressif's
   command-line installer.
2. Activate it in your shell (the path depends on your install):

   ```sh
   source ~/.espressif/tools/activate_idf_v6.1.sh
   idf.py set-target esp32c3   # first time only
   ```

The stack: ESP-IDF v6.1, [LVGL](https://lvgl.io) 9.6 through `esp_lvgl_port`,
and `esp_lcd` drivers for the GC9A01 panel and CST816S touch controller.
Exact component versions are pinned in `dependencies.lock`.

## Build

The default is the quiet **release** profile (log level WARN):

```sh
idf.py build
```

For development, layer `sdkconfig.dev` on top in a separate build directory.
It turns on INFO logs and the on-screen light-sleep statistics:

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.dev" -B build-dev build
```

The power-off hold time is under `idf.py menuconfig`, in the **OpenPuck** menu.
The auto-off time is a user setting, saved on the puck.

## Tests

Game logic lives in `components/puck_core`, which has no hardware dependencies
and is tested on your computer:

```sh
pip install gcovr     # once
tools/test.sh         # build, run tests (with sanitizers), coverage report
```

The run fails if line coverage of `puck_core` drops below 90%. GitHub Actions
runs the tests and the firmware build on every push. Add or update tests in
`test/host/` with your change.

The browser installer has its own tests (Node 22 or newer, no packages to
install), with a 90% line coverage gate:

```sh
tools/test-installer.sh
```

Add or update tests in `installer/test/` with changes to `installer/js/`. See
[`docs/installer.md`](docs/installer.md).

## Screenshots

The images in `docs/images/` are rendered from the real UI code on your
computer, with the hardware stubbed out and touches scripted:

```sh
tools/screenshots.sh              # all scenes
tools/screenshots.sh menu history # only these
```

It needs cmake and a C compiler, and fetches LVGL the first time. The scenes
live in `tools/screenshots/scenes.c`. **If your change alters the UI,
regenerate the screenshots and include them in your pull request.**

## Code layout

```
components/puck_core/  device-independent game logic (no ESP-IDF), unit-tested
main/board.h           pin map and hardware constants
main/power.c           power latch, debounced button, long-press off, idle auto-off
main/display.c         backlight, GC9A01 panel, CST816S touch, LVGL setup
main/storage.c         NVS persistence, written from a background task
main/battery_sense.c   battery voltage (ADC) to a smoothed percent
main/counter.c         game, history, timer and settings + when-to-save policy
main/ui*.c             counter screen, menu, history, settings, colors, timer page
main/main.c            startup order
test/host/             Unity unit tests for puck_core
installer/             browser installer (static site, no build step), JS tests in test/
tools/flash.py         back up, flash and restore over USB
tools/installer_site.py  assemble the installer site with the firmware to flash
tools/test-installer.sh  run the installer tests with coverage
tools/test.sh          run unit tests with a coverage report
tools/screenshots.sh   render docs/images/*.png from the real UI on the host
tools/screenshots/     host renderer: stubs, LVGL config, scripted scenes
tools/gen_fonts.sh     regenerate main/fonts/ from fonts/*.ttf (needs Node.js)
```

The rule of thumb: **`puck_core` holds the logic and is tested; `main/` stays
thin** (hardware and UI glue). If you can put a decision in `puck_core`, do.

Hardware facts (pin map, display and touch quirks, power behavior) are in
[`docs/hardware.md`](docs/hardware.md). Plans are in
[`docs/roadmap.md`](docs/roadmap.md).

## Style

- Match the surrounding code: naming, comment style, formatting.
- Use US English in code, comments and docs.
- Keep comments to the "why". Keep user-facing docs in plain words.

## Testing on a device

Build, then flash with `tools/flash.py`. **Back up your puck first** and keep
the backup:

```sh
python tools/flash.py backup my-puck-backup.bin
python tools/flash.py flash --backup my-puck-backup.bin
```

See [`docs/flashing.md`](docs/flashing.md) for restoring and for the
power-button hold needed while the original firmware is installed.

## Pull requests

1. Branch from `main`.
2. Make your change, with tests, regenerated screenshots (if the UI changed)
   and updated docs.
3. Open a pull request. **The title must be a
   [Conventional Commit](https://www.conventionalcommits.org/)**, such as
   `feat: add a low battery warning` or `fix: keep the timer paused after boot`.
   Pull requests are squash-merged, so the title becomes the commit message.
4. CI (tests and the firmware build) must pass.
5. A maintainer reviews and merges.

Releases are made automatically from those commit messages with
release-please. See [`docs/releasing.md`](docs/releasing.md).
