#pragma once

#include "lvgl.h"
#include "ui_theme.h"

// OpenPuck brand colors (see docs/brand/README.md).
#define LOGO_COLOR_RING    UI_COLOR_BONE     // #ECEBF5
#define LOGO_COLOR_CHEVRON UI_COLOR_VIOLET   // #A08CFF
#define LOGO_COLOR_SEAM    UI_COLOR_AMBER    // #FFB547

// Draw the OpenPuck mark, Split Duel (a ring, one chevron facing each player
// and the amber seam between them), with LVGL primitives, no bitmap.
// `diameter` is the outer diameter of the ring in pixels; 192 matches the
// 240x240 reference in docs/brand/openpuck-device.svg. Strokes thicken at
// small sizes, and below 24 px the seam is left out. Returns a non-clickable
// container of that size, so it doesn't block touches beneath.
lv_obj_t *logo_create(lv_obj_t *parent, int32_t diameter);
