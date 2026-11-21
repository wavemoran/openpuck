#!/bin/sh
# Regenerate the LVGL bitmap fonts in main/fonts/ from fonts/*.ttf.
# Only needed when changing fonts; the generated C files are committed, so
# normal builds don't need this.
# Requires Node.js; uses lv_font_conv 1.5.3 via npx.
#   tools/gen_fonts.sh
set -e
cd "$(dirname "$0")/.."
# Atkinson Hyperlegible Next Bold, from
# github.com/googlefonts/atkinson-hyperlegible-next (fonts/ttf/) at commit
# 7925f50f649b3813257faf2f4c0b381011f434f1. SIL Open Font License 1.1.
TTF=fonts/AtkinsonHyperlegibleNext-Bold.ttf
OUT=main/fonts
CONV="npx --yes lv_font_conv@1.5.3"
mkdir -p "$OUT"

gen() {  # name size range
    $CONV --font "$TTF" --size "$2" --bpp 4 --format lvgl --no-compress \
        --range "$3" --lv-include lvgl.h --lv-font-name "$1" -o "$OUT/$1.c"
}

DIGITS=0x30-0x39
gen puck_font_count_185 185 "$DIGITS"   # 1 player (drawn with letter spacing -10 so 0-99 fit)
gen puck_font_count_80  80  "$DIGITS"   # 2 players
gen puck_font_dial_80   80  "$DIGITS,0x4F,0x66"   # value dials: digits and "Off"
gen puck_font_small_32  32  "$DIGITS,0x2B,0x2D,0x3A"   # running change: +3
gen puck_font_timer_26  26  "$DIGITS,0x2B,0x2D,0x3A"   # timer: 54:59, +0:42
echo "Generated fonts in $OUT/"
