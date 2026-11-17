// Shared between ui.c, ui_counter.c and ui_menu.c; not part of the public API.
// All of these must be called on the LVGL task or under lvgl_port_lock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

// Counter screen (ui_counter.c)
// Build it on the given screen (1 or 2 player layout from the game state).
void ui_counter_start(lv_obj_t *screen);
// Re-read the game, timer and settings. Rebuilds the layout if the player
// count changed; new_game also hides the running changes.
void ui_counter_refresh(bool new_game, uint32_t now_ms);
// Periodic update: running changes, timer label, battery icon.
void ui_counter_tick(uint32_t now_ms);
// True while a running change is visible or a tap flash is fading: the UI
// needs fast updates.
bool ui_counter_animating(uint32_t now_ms);
// Forget the widgets before the screen is cleaned.
void ui_counter_stop(void);

// Menu (ui_menu.c)
void ui_menu_close(void);

// Full-screen panels opened from the menu (ui_history.c, ui_settings.c).
void ui_history_open(void);
void ui_settings_open(void);
// "Close if open" functions, used by ui_back(). Each returns true if it closed
// something. History and Settings also refresh the counter.
bool ui_history_close(void);
bool ui_settings_close(void);
bool ui_timer_set_close(void);       // refreshes the counter
bool ui_colors_close(void);       // back to Settings
bool ui_colors_picker_close(void);   // back to Colors, nothing saved
bool ui_menu_back(void);             // sub-ring to main ring, or main ring closed
// The Colors screen opened from Settings (ui_colors.c).
void ui_colors_open(void);
// The Set timer page opened from the Timer menu (ui_timer_set.c).
void ui_timer_set_open(void);

// Re-pick the UI tick period (ui.c): fast while animating, medium while the
// game timer runs or a save is pending, slow otherwise (every wakeup costs
// battery). Call after anything that can change those conditions.
void ui_tick_update(uint32_t now_ms);

// Shared label helper (ui.c)
lv_obj_t *ui_add_label(lv_obj_t *parent, const lv_font_t *font, const char *text);
// Shared by the full-screen panels (ui.c): an opaque black panel on the top
// layer that blocks touches, with the title at the top.
lv_obj_t *ui_panel_create(const char *title);
// A 40x2 violet bar centered under the panel title, in the gap above the list.
void ui_panel_add_underline(lv_obj_t *panel);
// The Holotable action button look: violet fill, dark ink text, radius 3.
void ui_style_button(lv_obj_t *btn);
// A "Done" button in parent that calls cb when clicked.
lv_obj_t *ui_add_done_button(lv_obj_t *parent, lv_event_cb_t cb);
