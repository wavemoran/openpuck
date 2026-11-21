# Menu rework: spec

Every option on OpenPuck is reachable through the quadrant ring, with no
scrolling lists and no more than three levels below the counter. This builds
on the Holotable restyle (`holotable-restyle.md`) and keeps its palette and
rules.

Reference images are in `menu-rework/`:

| File | What it is |
|---|---|
| `menu-rework/screens-board.png` | All 16 screens with notes (2x) |
| `menu-rework/screen-NN-*.png` | Each screen on its own at 240×240, 1:1 |

The references were drawn in HTML. In them, quadrant labels are straight text
rotated 40°; on the device they **keep curving along the rim** the way
`label_draw()` in `ui_menu.c` draws them now. **Where a reference and the
firmware differ in geometry, the firmware wins.** Use the references for
structure, content, color and finish.

## Rules

1. Colors come from `main/ui_theme.h`. **Amber `#FFB547` still means exactly
   two things:** the two-player seam and the running change. The 2P preview
   in the Colors swatch ring is a copy of the seam, so amber is allowed there.
2. Violet is the action and selection color. Use one font, as now.
3. A short press of the power button always goes back one step (see
   [Back stack](#back-stack)). The center of any ring is Back, except on the
   main ring, where it closes the menu.
4. Releasing inside the quadrant you pressed is what picks it, the same as
   today. Every new ring reuses the `ui_menu.c` machinery: backdrop, pressed
   band, separators, center ring, curved labels.
5. Existing saved settings and games must load. The only format change is the
   one new setting described below.

## New: value line on quadrant labels

Any quadrant can show a second, smaller line under its label for the current
value or state.

- Font `lv_font_montserrat_14`, color `UI_COLOR_MUTED`, curved like the main
  label and centered on the same mid-angle.
- It sits on the **center side** of the main label in every quadrant. For top
  quadrants that means a smaller radius than the label. For bottom quadrants,
  where glyph tops face the center, it also goes toward the center. Start
  around radius 78 and tune it on the device so it never touches the center
  ring (radius 46) or the main label.
- The same width assert applies as for labels: it has to fit inside the
  quadrant's span at its radius. Shorten the text rather than overflow.
- When a value changes in place (Battery, P2 faces), the line briefly turns
  `UI_COLOR_VIOLET` for about 400 ms, then fades back to muted. This is
  optional; leave it out if it complicates the tick logic.

## Screens

Positions are TL / TR / BL / BR = top-left, top-right, bottom-left,
bottom-right (`PUCK_ZONE_*`).

### 01 Main menu — unchanged
New game (TL), Timer (TR), Settings (BL), History (BR), logo in the center.

### 02 New game — rearranged, plus one new option
| Zone | Label | Value line | Action |
|---|---|---|---|
| TL | Reset | "keeps 1P" or "keeps 2P" | new game, same player count; closes |
| TR | P2 faces | "across" or "same way" | toggles the setting **in place**; the ring stays open and the value line updates |
| BL | 1 player | "playing now" if the current game is 1P | new 1P game; closes |
| BR | 2 players | "playing now" if the current game is 2P | new 2P game; closes |

P2 faces changes the counter **right away** (it calls
`ui_counter_refresh(false, …)`, with no new game) and is saved with the
settings. It's shown in 1P games too, so you can set it ahead of time.

### 03 Timer (timer on) — positions unchanged
| Zone | Label | Value line |
|---|---|---|
| TL | Start / Pause | "MM:SS left" (remaining time, or "+M:SS" in overtime) |
| TR | Reset | — |
| BL | Timer off | — |
| BR | "55 min" | "length" |

### 04 Timer (timer off)
Only **Timer on**, now in **TL** (it was BL), with the value line "55 min"
(the length it will start with).

### 05 Timer length — adds a rim arc
Today's page, plus a progress arc around the rim: an `lv_arc`, 6 px, track
`UI_COLOR_PANEL`, indicator `UI_COLOR_VIOLET`, rounded ends, starting at the
top and running clockwise. It fills
`(value − min) / (max − min)`. When the value is at the end of its range, that
end's − or + button is dimmed (fill `#0B0B12`, border `UI_COLOR_LINE`, glyph
`UI_COLOR_LINE_STRONG`) and ignores taps.

**Factor this page into a reusable value dial**: title, big value, unit line,
range line, a step function, the arc fraction, and a callback that applies the
value. Settings uses the same dial (screens 09–11). Timer length behaves as
today: tapping the number or pressing power returns to the counter.

*Revised after device testing:* the value is drawn at 64 px (80 px pushed "Off"
and "180" into the buttons), the title sits lower (y ≈ 42 center) so it clears
the arc, and a small muted chevron left of the title makes the title row a
Back target (≥ 44 px tall), same as tapping the number.

### 06 History — new player tags
On the default background only, the "P1"/"P2" text becomes a solid tag, 28×16,
radius 3:

| | Fill | Text |
|---|---|---|
| P1 | `#4FC3F7` (`UI_COLOR_P1_HISTORY`) | `#04131C` |
| P2 | `#FFE45C` (`UI_COLOR_P2_HISTORY`, **was violet**) | `#1F1A00` |

Add the two ink colors to `ui_theme.h`. On colored backgrounds, keep today's
per-player row colors. Rows, add/subtract colors and the rest of the layout
are unchanged.

### 07 Counter, two players, P2 facing the same way
When `p2_same_way` is on:
- The P2 (top) half is **not** rotated: its number, underline, running change
  and tap flash read upright, like P1's.
- The − and + hints and touch zones of the P2 half use the same screen sides as
  P1 (left subtracts, right adds).
- The seam, timer pill, battery and colors are unchanged.

When it's off, the layout is exactly what ships today.

### 08 Settings — the list becomes a ring
`ui_settings.c`'s scrolling list, its Done button and its Timer length row
are removed. Settings is now a menu level (for example
`ACTION_MENU_SETTINGS`) drawn by `ui_menu.c`:

| Zone | Label | Value line | Action |
|---|---|---|---|
| TL | Colors | — (or "P1 · P2" in 2P) | opens the swatch ring (13/14) |
| TR | Brightness | "60%" | opens the dial (09) |
| BL | Auto-off | "10 min" or "Off" | opens the dial (10/11) |
| BR | Battery | "Off", "Icon" or "Icon + %" | cycles Off → Icon → Icon + % → Off **in place** |

The center shows the Back chevron and "Back" as on the other sub-rings, plus
the firmware version (`esp_app_get_description()->version`, "v" + version) in
`lv_font_montserrat_14` or smaller, `UI_COLOR_MUTED`, under "Back". If it
doesn't fit inside the center ring, leave it out and put the version in the
Settings ring's value line for Colors instead; mention which you chose in the
PR.

### 09 Brightness dial
The value dial: title "Brightness", value "60", unit "%", range "10–100%",
steps from `puck_settings_step_brightness`. The **backlight changes live** as
you step. Tapping the number or pressing power returns to the Settings ring.

### 10–11 Auto-off dial
The value dial: title "Auto-off", value "10", unit "min idle", range line
"Off · 1 · 2 · 5 · 10 … 60". There are **ten stops: Off, 1, 2, 5, 10, 15, 20,
30, 45, 60.**
- Off sets `auto_off_enabled = false` and keeps `auto_off_minutes` unchanged.
  It shows the value "Off", the unit "stays on" and an empty arc.
- Pressing + from Off sets `auto_off_enabled = true` and `auto_off_minutes = 1`.
- Pressing − from 1 goes to Off.
- *Revised after device testing:* the dial **wraps**. + from 60 goes to Off
  and − from Off goes to 60, so neither button is ever dimmed on this dial.
- The arc fraction is stop index / 9.

**This is UI only.** The two existing fields keep their meaning, so there's no
format change. Put the stepping logic in `puck_core` (for example
`puck_settings_step_auto_off_dial(enabled, minutes, dir, &enabled_out,
&minutes_out)`) and unit-test it.

### 12 Battery
There's no screen; screen 12 is a state diagram. Tapping the quadrant cycles
`battery_display` and updates the value line, and the ring stays open.

### 13–14 Colors swatch ring
This replaces the P1/P2 tabs, the 3×3 preset grid and the Custom buttons in
`ui_colors.c`.

- **Ten swatches**, each 34 px across (radius 17), with centers on a circle of
  radius 94 around the screen center. Angles: 0° = right, increasing
  clockwise; start at 120° and step by 33.33°, so the last one sits at 60°.
  That leaves a gap at the bottom. Order: Default, Blue, Green, Red, Yellow,
  Blue text, Green text, Red text, Yellow text, Custom. Each preset swatch is
  drawn in its own colors with "20" in it. Black swatches get a 1.5 px
  `UI_COLOR_LINE_STRONG` outline. **Custom** is a multi-hue fill with a black
  "+".
- **The active preset** (the selected player's colors exactly match it) gets a
  2 px `UI_COLOR_VIOLET` ring at radius 21. Draw no ring if nothing matches.
- **Center preview**, radius 46, inside a 1.5 px `UI_COLOR_VIOLET_LINE` ring
  at radius 50:
  - 1P: a full circle in P1's colors showing "20".
  - 2P: split horizontally with a 2 px amber seam. The top half is P2 (rotated
    180° unless `p2_same_way` is on) and the bottom half is P1. Tapping a half
    selects that player, and the selected half gets a 2 px `UI_COLOR_VIOLET`
    outline. A small muted "P1"/"P2" label sits in the selected half. P1 is
    selected when the ring opens.
- **Done**: a violet button (`ui_style_button`), 50×24 drawn, centered in the
  bottom gap with its top at y = 208. Make its touch area at least 44 px tall.
  It returns to the Settings ring.
- **Tapping a preset**:
  - 1P: applies it and returns to the counter, as today.
  - 2P: applies it to the selected player and stays open.
- **Tapping Custom** opens screen 15 for the selected player.

### 15 Custom ring — two zones
A ring split into top and bottom halves with horizontal separators only, and
Back in the center.
- Top: "Background" with a 22 px dot in the current background color.
- Bottom: "Text" with a 22 px dot in the current text color, and a muted
  "for P1"/"for P2" line.

Labels here are straight (not curved) and centered, as drawn. Each half opens
the picker for that color. Back returns to the swatch ring.

### 16 Color picker — unchanged
Behavior and look as in the restyle. Saving or Cancel returns to the Custom
ring. Saving a custom background still picks black or white text
automatically.

## Back stack

| From | Short press of power / center / tapping the value |
|---|---|
| Main ring | close the menu |
| New game, Timer, Settings rings | main ring |
| Timer length dial | counter (as today) |
| Brightness, Auto-off dials | Settings ring |
| Swatch ring | Settings ring (same as Done) |
| Custom ring | swatch ring |
| Picker | Custom ring, nothing saved (Cancel) |
| History | counter (as today) |

## Storage

- `puck_settings_t` gains `bool p2_same_way;` at the end. The default is false
  (P2 faces across, as today).
- Saved format **version 6** = version 5's fields, then `p2_same_way` (0/1),
  then the crc32: **28 bytes**. Update the format comment in `puck_settings.h`,
  `encoded_size()`, encode/decode, `puck_settings_is_valid` and
  `PUCK_SETTINGS_ENCODED_MAX` if needed. Data from versions 1–5 decodes with
  `p2_same_way = false`.
- Add host tests in `test/host/` for v6 round-trip, v5 → v6 defaults, and the
  auto-off dial stepping (both ends, Off ↔ 1, wrapping 60 ↔ Off).

## Docs

- `docs/user-guide.md`: rewrite the menu, New game, Timer, Settings, Colors and
  History sections to match. Add P2 faces. Auto-off becomes one control with
  Off. Battery cycles in place. Timer length lives only in the Timer menu. P2
  is **yellow** in History.
- `docs/brand/holotable-restyle.md`: in the palette table, `P2_HISTORY`
  becomes `#FFE45C` (was violet); point to this file.
- Regenerate `docs/images/` with `tools/screenshots.sh`, adding scenes for
  the new screens (Settings ring, dials, swatch ring 1P/2P, Custom ring, 2P
  same way).
- Use a `feat:` PR title and leave `CHANGELOG.md` to release-please.

## Open question for the maintainer

When the timer is on, **Timer off stays in BL**, but when it's off,
**Timer on is in TL**, so the toggle changes corners. That's intentional for
now (Start/Pause keeps TL while the timer runs). Don't change it unless asked.

## Done when

- [ ] Every screen in `menu-rework/` is reachable, and it matches its
      reference in content, color and finish (geometry follows firmware
      conventions).
- [ ] No scrolling list is left in Settings or Colors.
- [ ] Power-button back follows the Back stack table everywhere.
- [ ] Amber appears only on the seam, the running change and the swatch
      ring's 2P preview seam.
- [ ] Settings saved by the current release load unchanged, and P2 faces
      defaults to across.
- [ ] `tools/test.sh` passes (coverage at or above 90%), the firmware builds,
      and `tools/screenshots.sh` renders every new scene.
- [ ] The user guide and the restyle spec are updated.
