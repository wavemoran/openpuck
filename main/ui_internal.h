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

// Menu (ui_menu.c): the quadrant rings.
void ui_menu_close(void);
bool ui_menu_back(void);             // sub-ring to main ring; main or Custom ring closed
void ui_menu_open_settings(void);    // the Settings ring (back from a dial or Colors)
void ui_menu_open_custom(void);      // the Custom color ring, over the swatch ring
void ui_menu_refresh(void);          // redraw the open ring's values

// History (ui_history.c), opened from the main ring.
void ui_history_open(void);
bool ui_history_close(void);         // refreshes the counter

// The value dial (ui_dial.c): a big value between "-" and "+" with a rim arc.
typedef struct {
    char value[8];       // the big value: digits, or "Off"
    char unit[16];       // the line under it
    uint16_t permille;   // rim arc fill, 0-1000
    bool at_min;         // "-" dimmed and ignored
    bool at_max;         // "+" dimmed and ignored
    int8_t preset;       // index of the preset pill matching the value, or -1
} ui_dial_state_t;
#define UI_DIAL_MAX_PRESETS 2
typedef struct {
    const char *title;
    const char *range;               // the range line near the bottom (no presets)
    const char *presets[UI_DIAL_MAX_PRESETS]; // pill labels in place of the range line;
    uint8_t preset_count;            //   0 shows the range line instead
    void (*apply_preset)(uint8_t index);    // a pill was tapped
    void (*read)(ui_dial_state_t *state);   // the current value
    void (*step)(int dir);           // apply one step down (-1) or up (+1)
    void (*closed)(void);            // after closing: where to go back to
} ui_dial_t;
void ui_dial_open(const ui_dial_t *dial);
bool ui_dial_close(void);

// The timer length dial, opened from the Timer ring (ui_timer_set.c).
void ui_timer_set_open(void);

// Settings ring actions (ui_settings.c).
void ui_settings_open_brightness(void);
void ui_settings_open_auto_off(void);
void ui_settings_cycle_battery(void);    // Off -> Icon -> Icon + % -> Off
const char *ui_settings_battery_text(uint8_t battery_display);

// Colors (ui_colors.c): the swatch ring, opened from the Settings ring, and
// the custom picker, opened from the Custom ring.
void ui_colors_open(void);
bool ui_colors_close(void);          // back to the Settings ring
uint8_t ui_colors_player(void);      // the player being edited (0 = P1)
void ui_colors_open_picker(bool background);
bool ui_colors_picker_close(void);   // back to the Custom ring, nothing saved

// Re-pick the UI tick period (ui.c): fast while animating, medium while the
// game timer runs or a save is pending, slow otherwise (every wakeup costs
// battery). Call after anything that can change those conditions.
void ui_tick_update(uint32_t now_ms);

// Shared label helper (ui.c)
lv_obj_t *ui_add_label(lv_obj_t *parent, const lv_font_t *font, const char *text);
// Shared by the full-screen panels (ui.c): an opaque black panel on the top
// layer that blocks touches, with the title at the top (none if NULL).
lv_obj_t *ui_panel_create(const char *title);
// A 40x2 violet bar centered under the panel title, in the gap above the list.
void ui_panel_add_underline(lv_obj_t *panel);
// The Holotable action button look: violet fill, dark ink text, radius 3.
void ui_style_button(lv_obj_t *btn);
// A "Done" button in parent that calls cb when clicked.
lv_obj_t *ui_add_done_button(lv_obj_t *parent, lv_event_cb_t cb);
