# Holotable restyle: spec

OpenPuck adopts the "Holotable" design language from SWU TCC (a void field,
hairline frames, one violet action color, amber reserved for one meaning) and
a new mark, **Split Duel**. This is a **restyle, not a redesign**: the device
UI is already tuned, so layouts stay exactly as they are.

Reference images live next to this file in `restyle/`:

| File | What it is |
|---|---|
| `restyle/logo-board.png` | Logo system: dark, light, mono, lockups, app icon, small sizes (2x) |
| `restyle/screens-board.png` | All 12 screens side by side, with notes (2x) |
| `restyle/screen-NN-*.png` | Each screen alone at 240×240, 1:1, comparable with `tools/screenshots.sh --no-bezel` |

The references were drawn in HTML from the current screenshots, so positions
are approximate. **Where a reference and the current firmware disagree on
position or size, the firmware wins.** Use the references for color, outline
and finish only.

## Hard rules

1. **Do not change** any position, size, font size, touch area, gesture,
   timing or behavior. No new screens, no reflow.
2. **One font:** Atkinson Hyperlegible Next Bold (`fonts/`), as now. Don't add
   fonts to the firmware.
3. **Player colors stay the user's.** Presets, custom colors, tap flashes and
   the "colored background" code paths (`chrome_custom()`, `text_of()`) keep
   working exactly as today.
4. **Amber `#FFB547` means exactly two things:** the two-player seam and the
   running change. Nothing else is amber.
5. **Violet `#A08CFF` is the action color:** buttons, selected state, outlines
   of interactive pills and the center button, the pause icon.
6. Keep `PUCK_COLOR_BLACK` and every saved-settings value unchanged, so
   existing saved games and settings load as before.

## Palette

LVGL draws in RGB565, so translucent tints below are given as **pre-blended
solid colors** over black. Use the solid value; don't rely on opacity.

| Token | Hex | Use |
|---|---|---|
| `BG` | `#000000` | Screen background (unchanged) |
| `PANEL` | `#171724` | Neutral button fill, unselected tab, list row fill |
| `LINE` | `#262636` | Row borders |
| `LINE_STRONG` | `#3A3A52` | Menu dividers, unselected tab and preset borders |
| `BONE` | `#ECEBF5` | Logo ring, menu text, primary text (replaces `#F5F2EA`) |
| `MUTED` | `#9A98B3` | Secondary text: "min", ranges, version label |
| `VIOLET` | `#A08CFF` | Buttons, selected state, pause icon, title underline |
| `VIOLET_INK` | `#0E0820` | Text on violet buttons |
| `VIOLET_LINE` | `#605499` | Violet at 60% over black: timer pill and center-button outlines |
| `VIOLET_PRESSED` | `#13111F` | Violet at 12% over black: menu pressed band |
| `AMBER` | `#FFB547` | Two-player seam, running change |
| `OVERTIME` | `#FF7A59` | Overtime timer text and pill outline (was palette red) |
| `P1_HISTORY` | `#4FC3F7` | P1 tag fill in History on the default background (unchanged); ink `#04131C` |
| `P2_HISTORY` | `#FFE45C` | P2 tag fill in History on the default background (was violet `#A08CFF`, before that amber `#FFB74D`; see [menu-rework.md](menu-rework.md)); ink `#1F1A00` |

Consider collecting these in one header (e.g. `main/ui_theme.h`) instead of
scattering hex values, since several files repeat the same greys today.

## Logo: Split Duel

A circle, two chevrons (one facing each player) and an amber seam held
inside the circle. Built only from a circle, two polylines and one line, so
the firmware draws it with LVGL (no bitmap), as `main/logo.c` does today.

### Geometry

Defined on a 64×64 grid, center (32, 32). Scale everything by
`s = diameter / 51` (51 = outer diameter of the ring on this grid), so
`logo_create(parent, 192)` gives a ring with outer diameter 192 like today.

| Part | Geometry (64 grid) | Stroke | Color |
|---|---|---|---|
| Ring | circle r = 24 (center line) | 3 | `BONE` |
| Top chevron | (21, 25) → (32, 17) → (43, 25), miter join, butt caps | 5 | `VIOLET` |
| Bottom chevron | (21, 39) → (32, 47) → (43, 39), mirror of the top | 5 | `VIOLET` |
| Seam | (12, 32) → (52, 32), butt caps, inside the ring | 2.5 | `AMBER` |

On the 240 screen grid at diameter 192 (s ≈ 3.765, offset so the center is
(120, 120)): ring center-line radius ≈ 90, stroke ≈ 11; chevron stroke ≈ 19;
seam stroke ≈ 9.4, from x ≈ 45 to x ≈ 195.

### Small sizes

Strokes thicken as the mark shrinks (all values on the 64 grid):

| Rendered size | Ring | Chevrons | Seam |
|---|---|---|---|
| ≥ 64 px (splash 184–192) | 3 | 5 | 2.5, x 12→52 |
| 48 px (menu center, lockup) | 4 | 6 | 3.5, x 13→51 |
| 32 px | 5 | 7 | 4.5, x 14→50 |
| 16 px (favicon) | 7 | 9, points (20,24)(32,16)(44,24) and mirror | **none** |

`logo_create()` should pick the row by `diameter`. The menu center uses about
56 px inside its 84 px button.

### LVGL notes

- Ring: `lv_arc` with full 0–360 bg angles, or a transparent object with
  `LV_RADIUS_CIRCLE` and a border; either is fine.
- Chevrons: `lv_line` with three points. LVGL lines don't miter at the
  joint; if the apex looks notched, draw each chevron as two segments that
  overlap at the apex, or as a filled polygon. Match the reference at 192 and
  48 px; small differences at the apex are acceptable.
- Keep the returned object non-clickable, as now.
- Rename colors in `logo.h`: `LOGO_COLOR_RING` → `#ECEBF5`; replace
  `LOGO_COLOR_BEAD` with `LOGO_COLOR_CHEVRON` (`#A08CFF`) and
  `LOGO_COLOR_SEAM` (`#FFB547`). `ui_menu.c` uses `LOGO_COLOR_RING` as
  `MENU_COLOR_TEXT`; that keeps working.

### Variants (docs/brand, via `tools/gen_logo.py`)

| Variant | Ring | Chevrons | Seam | Background |
|---|---|---|---|---|
| Dark (default) | `#ECEBF5` | `#A08CFF` | `#FFB547` | `#0F0F17` or transparent |
| Light | `#0F0F17` | `#6247E0` | `#D98A00` | `#F6F5FB` or white |
| Mono | `currentColor` | `currentColor` | `currentColor` | none |

### Wordmark and lockup

- Text: **OPENPUCK**, all caps, Atkinson Hyperlegible Next Bold, tracking
  +6% (0.06 em). "OPEN" in `BONE` (light: `#0F0F17`), "PUCK" in `VIOLET`
  (light: `#6247E0`).
- Optional tagline: **LIFE COUNTER FIRMWARE**, same font, about 0.3× the
  wordmark size, tracking 0.22 em, `MUTED` (light: `#4D4B66`).
- The mark sits to the left of the text, about 1.75× the text's font size
  tall, with a gap of about half the font size, vertically centered.
- The old "mark replaces the O" rule is retired: the new mark is not an O.
- Outline the text to paths, as `gen_logo.py` does now.

### App icon / avatar

Rounded square, fill `#0F0F17`, corner radius ≈ 21% of the side, a 2 px
inner stroke of violet at 30% (`#3A345D` solid on `#0F0F17`), mark at ≈ 70%
of the side, using the 48 px stroke row.

## Screens

Each item lists **only** what changes. Everything else stays.

### 01 Boot splash (`screen-01-boot-splash.png`)
- New mark, same size and position as the current logo (outer diameter 192).

### 02 Counter, one player (`screen-02-counter.png`)
- Timer pill border: `#6E6E6E` → `VIOLET_LINE` (keep the custom-color branch
  that uses `text_of(0)`).
- Pause icon: grey → `VIOLET` (custom-color branch unchanged).
- Life total, battery, underline: unchanged.

### 03 Running change (`screen-03-running-change.png`)
- Running change text ("+2", "−3") on the default background: green/red →
  `AMBER` for both signs. On colored backgrounds keep `text_of(player)`.
- Tap flash (green add, red subtract): unchanged.

### 04 Two players (`screen-04-two-players.png`)
- New: a 2 px `AMBER` line across the full width on the boundary between the
  halves, drawn **behind** the timer pill and battery. Only in two-player
  mode. If the halves use player backgrounds that make amber hard to see,
  keep it anyway; it's 2 px.
- Timer pill: same changes as 02, centered on the seam as now.
- −/+ hints, numbers, colors: unchanged.

### 05 Overtime (`screen-05-overtime.png`)
- Overtime text: palette red → `OVERTIME`, and its pill border → `OVERTIME`
  too. Keep today's rule that a colored one-player background uses the text
  color but still shows the plus sign.

### 06 Menu (`screen-06-menu.png`)
- Dividers: `#3A3D42` → `LINE_STRONG`.
- Pressed band: `#2E3238` → `VIOLET_PRESSED`.
- Center button outline: `BONE` → `VIOLET_LINE`.
- Center logo: the new mark at the 48 px stroke row (≈ 56 px).
- Labels: `BONE`, positions and curve unchanged.

### 07 New game and Timer submenus (`screen-07-new-game.png`)
- Same as 06. "Back" chevron in `VIOLET`, the word in `BONE`.

### 08 Timer length (`screen-08-timer-length.png`)
- −/+ buttons: fill `#3A3D42` → `PANEL`, plus a 1.5–2 px `VIOLET_LINE`
  border. Glyphs `BONE`.
- Unit color `#9A9DA3` → `MUTED`.

### 09 History (`screen-09-history-default-colors.png`)
- `PLAYER_2_COLOR` `#FFB74D` → `#A08CFF`.
- Rows on the default background: fill `PANEL`, 1 px `LINE` border,
  radius 3. Keep add/subtract colors as they are now. On colored
  backgrounds keep today's per-player row colors.
- Title underline: 40×2 `VIOLET` bar centered under "History", placed in the
  existing gap without moving the list. If it doesn't fit, leave it out.

### 10 Settings (`screen-10-settings.png`)
- All buttons (Colors, −/+, Battery, Auto-off, Done): fill `VIOLET`, text
  `VIOLET_INK`, radius 3 (currently LVGL theme blue).
- The Auto-off switch: checked indicator `VIOLET`.
- Section labels `BONE`; version label `#8A8D92` → `MUTED`.
- Title underline as in 09, same "only if it fits" rule.
- Dimmed "Auto-off after" row: keep the current dimming.

### 11 Colors (`screen-11-colors.png`)
- P1/P2 tabs: selected = `VIOLET` fill + `VIOLET_INK` text; unselected =
  `PANEL` fill + 1 px `LINE_STRONG` border + `BONE` text
  (`COLOR_UNSELECTED` `#3A3D42` → these).
- Preset circles: the outline on black presets `#6E6E6E` → `LINE_STRONG`.
  The preset that matches the current colors gets a 2 px `VIOLET` ring
  with a 2 px gap, if the screen already knows which one is active; skip it
  otherwise.
- Custom background / Custom text / Done buttons: as Settings.

### 12 Color picker (`screen-12-color-picker.png`)
- Hue ring, preview and tint slider unchanged in behavior.
- Preview circle: a 1.5 px `VIOLET_LINE` ring outside it.
- Tint knob border `VIOLET`; Cancel button as Settings buttons.

## Docs and assets to update

- `tools/gen_logo.py`: new mark, variants, lockups, avatar, social preview
  (dark field, lockup centered). Regenerate everything in `docs/brand/`,
  including PNGs. Keep the file names so README links don't break.
- `docs/brand/README.md`: new concept ("one chevron per player, split by the
  amber seam"), colors table (this file's palette), geometry, small-size
  table, wordmark rules, updated don'ts:
  - Don't let the seam touch or cross the ring.
  - Don't use amber for anything but the seam (in the logo) and the seam and
    running change (on the device).
  - Don't use the MetalFab Tokens or Life Puck names in the logo.
- `docs/user-guide.md`, History: "P2 is amber" → "P2 is violet". Running
  change: mention that it shows in amber on the default background.
- `docs/images/`: regenerate with `tools/screenshots.sh`.
- `CHANGELOG.md`: leave to release-please; use a `feat:` PR title.

## Done when

- [ ] `tools/screenshots.sh` renders every scene; each matches its
      `restyle/screen-NN-*.png` in color and finish, and matches the
      **current** screenshots in layout (diff positions, not colors).
- [ ] No amber anywhere except the 2P seam and the running change (grep for
      `FFB547`, `FFB74D` and palette orange/amber).
- [ ] Logo is crisp at 192 (splash) and ≈ 56 (menu center) on the device
      or host renderer; the 16 px favicon drops the seam.
- [ ] Custom-color paths still use the player's text color for chrome.
- [ ] Host tests pass (`tools/test.sh`), firmware builds, and saved settings
      from the current release load unchanged.
- [ ] `docs/brand/` regenerated by `tools/gen_logo.py`, README and user guide
      updated.
