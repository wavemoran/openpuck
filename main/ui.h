#pragma once

#include <stdbool.h>

#include "lvgl.h"

// Counter screen. Tap top/bottom half for +1/-1, hold for +5/-5; hold the
// center for the menu.
void ui_start(lv_indev_t *touch);

// Open the radial game menu. Call only on the LVGL task (or under the LVGL
// lock, e.g. via lv_async_call).
void ui_open_menu(void);

// Re-read the game, timer and settings into the counter screen. Call only on
// the LVGL task.
void ui_refresh_counter(void);

// Power-button "back": go back one step from the topmost open layer (value
// dial, color picker, menu ring, Colors, History) and return true, or return
// false if nothing is open. Call only on the LVGL task.
bool ui_back(void);

// Replace the screen with a full-screen message. Safe to call from any task;
// gives up if the UI is busy so callers never block on a hung UI.
void ui_show_message(const char *text);
