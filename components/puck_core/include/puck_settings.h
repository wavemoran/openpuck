// User settings. Designed to grow: add fields at the end and bump the saved
// version; decoding an older version fills new fields with defaults.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "puck_state_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PUCK_AUTO_OFF_DEFAULT_MINUTES 10
#define PUCK_TIMER_DEFAULT_MINUTES    55
#define PUCK_TIMER_MIN_MINUTES        5
#define PUCK_TIMER_MAX_MINUTES        180
#define PUCK_TIMER_STEP_MINUTES       5
#define PUCK_BRIGHTNESS_DEFAULT       60
#define PUCK_BRIGHTNESS_MIN           10
#define PUCK_BRIGHTNESS_MAX           100
#define PUCK_BRIGHTNESS_STEP          10   // valid values are multiples of this

typedef enum {
    PUCK_BATTERY_DISPLAY_OFF = 0,
    PUCK_BATTERY_DISPLAY_ICON,
    PUCK_BATTERY_DISPLAY_ICON_PERCENT,
} puck_battery_display_t;

typedef struct {
    bool auto_off_enabled;       // default true
    uint8_t auto_off_minutes;    // one of the auto-off choices; default 10
    uint8_t timer_minutes;       // PUCK_TIMER_MIN..MAX in steps of 5; default 55
    uint8_t battery_display;     // puck_battery_display_t; default ICON
    // Per-player counter colors, 0xRRGGBB (player 0 is also the 1-player
    // color). Default: black background, white text.
    uint32_t player_background[PUCK_MAX_PLAYERS];
    uint32_t player_text[PUCK_MAX_PLAYERS];
    bool timer_enabled;          // show and use the timer; default true
    uint8_t brightness;          // backlight percent, PUCK_BRIGHTNESS_MIN..MAX in steps of 10; default 60
    bool p2_same_way;            // 2 players: P2's half reads upright like P1's; default false (faces across)
} puck_settings_t;

void puck_settings_defaults(puck_settings_t *settings);
bool puck_settings_is_valid(const puck_settings_t *settings);

// Auto-off choices, in minutes: 1, 2, 5, 10, 15, 20, 30, 45, 60.
// Step to the next (dir > 0) or previous (dir < 0) choice, clamped at the
// ends. A value that isn't a choice steps to the nearest choice in that
// direction.
uint8_t puck_settings_step_auto_off(uint8_t minutes, int dir);

// The Auto-off dial: one control with PUCK_AUTO_OFF_DIAL_STOPS stops, Off then
// each auto-off choice. It maps onto the two stored fields without changing
// their meaning: Off is auto_off_enabled = false with auto_off_minutes kept.
#define PUCK_AUTO_OFF_DIAL_STOPS 10

// Stop index 0 (Off) .. PUCK_AUTO_OFF_DIAL_STOPS - 1 (60 min). Minutes that
// aren't a choice count as the nearest choice.
uint8_t puck_settings_auto_off_dial_index(bool enabled, uint8_t minutes);

// Step the dial one stop up (dir > 0) or down (dir < 0), clamped at both ends.
// + from Off turns auto-off on at the first choice (1 min); - from the first
// choice turns it off and keeps the minutes. dir == 0 changes nothing except
// snapping minutes to the nearest choice.
void puck_settings_step_auto_off_dial(bool enabled, uint8_t minutes, int dir,
                                      bool *enabled_out, uint8_t *minutes_out);

// Step the timer duration by PUCK_TIMER_STEP_MINUTES, clamped to the range.
uint8_t puck_settings_step_timer(uint8_t minutes, int dir);

// Step the brightness by PUCK_BRIGHTNESS_STEP percent, clamped to the range.
uint8_t puck_settings_step_brightness(uint8_t percent, int dir);

// Saved form (versioned, CRC-protected, magic "OPST"):
//   version 1: header (5), auto_off_enabled, auto_off_minutes, timer_minutes,
//              battery_graphic bool, crc32 (13 bytes)
//   version 2: same layout; byte 8 is battery_display (v1: 0 -> OFF, 1 -> ICON)
//   version 3: version 2's fields, then for each player in order
//              background R, G, B and text R, G, B (12 bytes), crc32 (25 bytes)
//   version 4: version 3's fields, then timer_enabled (0/1),
//              crc32 (26 bytes)
//   version 5: version 4's fields, then brightness (percent),
//              crc32 (27 bytes)
//   version 6 (current): version 5's fields, then p2_same_way (0/1),
//              crc32 (28 bytes)
// Older versions decode with defaults for the fields they lack.
#define PUCK_SETTINGS_ENCODED_MAX 32
size_t puck_settings_encode(const puck_settings_t *settings, uint8_t *buf, size_t cap);
// *settings is untouched on error (caller falls back to defaults).
puck_state_result_t puck_settings_decode(const uint8_t *buf, size_t len, puck_settings_t *settings);

#ifdef __cplusplus
}
#endif
