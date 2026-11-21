// Holotable palette (docs/brand/holotable-restyle.md, docs/brand/README.md).
// LVGL draws in RGB565, so translucent tints are pre-blended solid colors
// over black: use these values, not opacity.
#pragma once

#include "lvgl.h"

#define UI_HEX_BONE   0xECEBF5
#define UI_HEX_VIOLET 0xA08CFF
#define UI_HEX_AMBER  0xFFB547

#define UI_COLOR_BG             lv_color_hex(0x000000)   // screen background
#define UI_COLOR_PANEL          lv_color_hex(0x171724)   // neutral fill, unselected tab, list row
#define UI_COLOR_LINE           lv_color_hex(0x262636)   // row borders
#define UI_COLOR_LINE_STRONG    lv_color_hex(0x3A3A52)   // menu dividers, unselected outlines
#define UI_COLOR_BONE           lv_color_hex(UI_HEX_BONE) // primary text, logo ring
#define UI_COLOR_MUTED          lv_color_hex(0x9A98B3)   // secondary text
#define UI_COLOR_VIOLET         lv_color_hex(UI_HEX_VIOLET) // the action color
#define UI_COLOR_VIOLET_INK     lv_color_hex(0x0E0820)   // text on violet
#define UI_COLOR_VIOLET_LINE    lv_color_hex(0x605499)   // violet 60% over black: outlines
#define UI_COLOR_VIOLET_PRESSED lv_color_hex(0x13111F)   // violet 12% over black: pressed band
// Amber means exactly two things: the two-player seam and the running change.
#define UI_COLOR_AMBER          lv_color_hex(UI_HEX_AMBER)
#define UI_COLOR_OVERTIME       lv_color_hex(0xFF7A59)   // overtime timer text and pill
#define UI_COLOR_P1_HISTORY     lv_color_hex(0x4FC3F7)   // P1 tag fill in History (default background)
#define UI_COLOR_P1_HISTORY_INK lv_color_hex(0x04131C)   // P1 tag text
#define UI_COLOR_P2_HISTORY     lv_color_hex(0xFFE45C)   // P2 tag fill in History (default background)
#define UI_COLOR_P2_HISTORY_INK lv_color_hex(0x1F1A00)   // P2 tag text
#define UI_COLOR_DISABLED_FILL  lv_color_hex(0x0B0B12)   // dimmed dial button at the end of its range

#define UI_BUTTON_RADIUS 3
