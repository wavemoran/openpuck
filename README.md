<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/brand/openpuck-lockup.svg">
    <img src="docs/brand/openpuck-lockup-light.svg" alt="OpenPuck" width="360">
  </picture>
</p>

<p align="center">
  <a href="https://github.com/wavemoran/openpuck/actions/workflows/ci.yml"><img src="https://github.com/wavemoran/openpuck/actions/workflows/ci.yml/badge.svg?branch=main" alt="CI"></a>
  <a href="https://scorecard.dev/viewer/?uri=github.com/wavemoran/openpuck"><img src="https://api.scorecard.dev/projects/github.com/wavemoran/openpuck/badge" alt="OpenSSF Scorecard"></a>
</p>

# OpenPuck

OpenPuck is free, open-source software for your Life Puck, the round-screen
counter used for card games. It gives you a fast, easy-to-read life counter for
one or two players, with a game timer, a history of changes, your choice of
colors and a battery indicator. It is built to be reliable first: the screen
answers your touch right away, your game is saved in the background, and the
puck turns off only when you tell it to or when it has been left alone.

OpenPuck is an independent project. It is not affiliated with or endorsed by
MetalFab Tokens. "Life Puck" is used only to describe compatible hardware.

| One player | Two players | Menu | Colors |
|:---:|:---:|:---:|:---:|
| <img src="docs/images/counter-1p.png" width="180" alt="One player counter showing 20 with the timer below"> | <img src="docs/images/counter-2p.png" width="180" alt="Two player counter, red and blue halves, player 2 upside down"> | <img src="docs/images/menu.png" width="180" alt="Menu with New game, Timer, History and Settings"> | <img src="docs/images/colors.png" width="180" alt="Colors screen with player buttons and color presets"> |

## What it does

- **Count life** for one or two players. Tap to add or subtract 1, hold to
  change by 5. Numbers stay between 0 and 99.
- **Two players** share the screen, each half facing its player.
- **Running change** shows how much you gained or lost in a burst of taps.
- **Timer** counts down, then into red overtime. Start and pause it with the
  power button.
- **History** lists every change since the game began.
- **Colors:** presets (Blue, Green, Red, Yellow and more) or your own, for each
  player.
- **Battery** indicator, adjustable brightness, and auto-off.
- **Remembers** your game and settings when you turn it off.

## Get started

1. **[Install from your browser](https://wavemoran.github.io/openpuck/):**
   back up your puck, then install OpenPuck, with nothing to set up. The
   [install guide](docs/install.md) has the steps, supported browsers and a
   command-line alternative.
2. **[User guide](docs/user-guide.md):** how every screen and setting works.
3. Stuck? See [troubleshooting](docs/troubleshooting.md) and
   [SUPPORT.md](SUPPORT.md).

## Compatibility

OpenPuck is made for the **MetalFab Tokens Life Puck**. It is built on its
parts: an ESP32-C3 processor, a GC9A01 round 240x240 screen and a CST816S
touch controller. Other devices that use the same parts may be supported
later.

## Before you install

**Back up your puck first.** The backup is the only way to go back to the
software it came with, and the install tool will not run without one.
Installing is at your own risk. See the [install guide](docs/install.md).

## For developers

OpenPuck is written in C on [ESP-IDF](https://github.com/espressif/esp-idf)
v6.1 with [LVGL](https://lvgl.io) 9.6. The game logic is in
`components/puck_core` (no hardware, unit-tested); `main/` is the thin
hardware and screen layer.

| Part | Detail |
|---|---|
| Processor | ESP32-C3 (RISC-V, Wi-Fi + BLE), 4 MB flash |
| Display | GC9A01 240x240 round LCD, SPI |
| Touch | CST816S, I2C |
| Power | Battery, with a GPIO-held power latch and a power button |

Quick start:

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh   # path depends on your install
idf.py set-target esp32c3   # first time only
idf.py build                # release profile
tools/test.sh               # host unit tests with a 90% coverage gate
tools/screenshots.sh        # regenerate docs/images/*.png
```

- [CONTRIBUTING.md](CONTRIBUTING.md): setup, the dev build profile, tests,
  screenshots, code layout and the pull request flow.
- [docs/releasing.md](docs/releasing.md): how releases are made.
- [docs/roadmap.md](docs/roadmap.md): browser installer, tests and releases.
- [docs/hardware.md](docs/hardware.md): pin map and hardware quirks.
- [docs/flashing.md](docs/flashing.md): back up, flash and restore with
  `tools/flash.py`.

The same parts (ESP32-C3, GC9A01 and CST816S) are used on several off-the-shelf
round display boards, so a board-config layer is planned so those can run
OpenPuck too. Exact component versions are pinned in `dependencies.lock`.

## Credits

- Logo: see [`docs/brand/`](docs/brand/README.md).
- Digits use [Atkinson Hyperlegible Next](https://github.com/googlefonts/atkinson-hyperlegible-next)
  Bold by the Braille Institute, under the SIL Open Font License 1.1
  ([`fonts/OFL.txt`](fonts/OFL.txt)). Regenerate the bitmap fonts with
  `tools/gen_fonts.sh`.
- Third-party software is listed in [NOTICE](NOTICE).

## License

OpenPuck is licensed under the [Apache License 2.0](LICENSE).
