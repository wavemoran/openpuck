// Maps touches to life changes, and tracks the running change shown on screen
// (e.g. "+3") until the player pauses.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PUCK_TAP_STEP        1
#define PUCK_LONG_PRESS_STEP 5
// Pressing and holding within this many pixels of the screen center opens the menu.
#define PUCK_CENTER_RADIUS   45

// Top half of the screen raises the total, bottom half lowers it.
// long_press selects the larger step.
int16_t puck_touch_delta(int32_t y, int32_t screen_height, bool long_press);

// Which player and change a touch at (x, y) means. 1 player: top half +, bottom
// half -. 2 players: bottom half is player 0, top half is player 1; in each
// half the player's own left is -, right is +. Player 1 is shown rotated 180
// degrees, so their left is screen-right, unless p2_same_way: then they read
// upright and both halves use screen-left for - and screen-right for +.
// Returns false if player_count isn't 1 or 2.
bool puck_touch_target(int32_t x, int32_t y, int32_t width, int32_t height,
                       uint8_t player_count, bool p2_same_way, bool long_press,
                       uint8_t *player, int16_t *delta);

// True when (x, y) is within PUCK_CENTER_RADIUS of the screen center
// (inclusive).
bool puck_touch_in_center(int32_t x, int32_t y, int32_t width, int32_t height);

typedef enum {
    PUCK_ZONE_CENTER = 0,
    PUCK_ZONE_TOP_LEFT,
    PUCK_ZONE_TOP_RIGHT,
    PUCK_ZONE_BOTTOM_RIGHT,
    PUCK_ZONE_BOTTOM_LEFT,
} puck_zone_t;

// Which menu zone a touch at (x, y) is in: the center circle
// (PUCK_CENTER_RADIUS, same as puck_touch_in_center) or one of four
// quadrants split by the vertical and horizontal center lines. Like
// puck_touch_target, x == width / 2 counts as right and y == height / 2 as
// bottom.
puck_zone_t puck_touch_zone(int32_t x, int32_t y, int32_t width, int32_t height);

// Angle in degrees (0-359, rounded to the nearest) of (x, y) around the
// screen center: 0 = right (3 o'clock), increasing clockwise on screen
// (y grows downward), like LVGL's arc angles. The exact center returns 0.
uint16_t puck_touch_angle(int32_t x, int32_t y, int32_t width, int32_t height);

typedef struct {
    uint32_t show_ms;      // how long the running change stays visible
    int32_t total;
    uint32_t last_change_ms;
    bool active;
} puck_change_tracker_t;

void puck_change_tracker_init(puck_change_tracker_t *tracker, uint32_t show_ms);

// Record a change; returns the running total to display.
int32_t puck_change_tracker_add(puck_change_tracker_t *tracker, int16_t delta, uint32_t now_ms);

// True while the running change should still be shown. Once it expires the
// total resets, so the next change starts a new run.
bool puck_change_tracker_visible(puck_change_tracker_t *tracker, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
