#include "puck_touch_input.h"

#include <math.h>

int16_t puck_touch_delta(int32_t y, int32_t screen_height, bool long_press)
{
    int16_t step = long_press ? PUCK_LONG_PRESS_STEP : PUCK_TAP_STEP;
    return y < screen_height / 2 ? step : (int16_t)-step;
}

bool puck_touch_target(int32_t x, int32_t y, int32_t width, int32_t height,
                       uint8_t player_count, bool p2_same_way, bool long_press,
                       uint8_t *player, int16_t *delta)
{
    if (player_count == 1) {
        *player = 0;
        *delta = puck_touch_delta(y, height, long_press);
        return true;
    }
    if (player_count != 2) {
        return false;
    }
    const int16_t step = long_press ? PUCK_LONG_PRESS_STEP : PUCK_TAP_STEP;
    const bool bottom = y >= height / 2;
    const bool right_of_center = x >= width / 2;
    // A top player facing the other way has their left and right swapped.
    const bool plus = bottom || p2_same_way ? right_of_center : !right_of_center;
    *player = bottom ? 0 : 1;
    *delta = plus ? step : (int16_t)-step;
    return true;
}

bool puck_touch_in_center(int32_t x, int32_t y, int32_t width, int32_t height)
{
    const int32_t dx = x - width / 2;
    const int32_t dy = y - height / 2;
    return dx * dx + dy * dy <= PUCK_CENTER_RADIUS * PUCK_CENTER_RADIUS;
}

puck_zone_t puck_touch_zone(int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (puck_touch_in_center(x, y, width, height)) {
        return PUCK_ZONE_CENTER;
    }
    const bool bottom = y >= height / 2;
    const bool right_of_center = x >= width / 2;
    if (bottom) {
        return right_of_center ? PUCK_ZONE_BOTTOM_RIGHT : PUCK_ZONE_BOTTOM_LEFT;
    }
    return right_of_center ? PUCK_ZONE_TOP_RIGHT : PUCK_ZONE_TOP_LEFT;
}

uint16_t puck_touch_angle(int32_t x, int32_t y, int32_t width, int32_t height)
{
    const double dx = (double)(x - width / 2);
    const double dy = (double)(y - height / 2);
    double degrees = atan2(dy, dx) * (180.0 / 3.14159265358979323846);
    if (degrees < 0) {
        degrees += 360.0;
    }
    return (uint16_t)((int)(degrees + 0.5) % 360);
}

void puck_change_tracker_init(puck_change_tracker_t *tracker, uint32_t show_ms)
{
    *tracker = (puck_change_tracker_t) {
        .show_ms = show_ms,
    };
}

int32_t puck_change_tracker_add(puck_change_tracker_t *tracker, int16_t delta, uint32_t now_ms)
{
    if (!puck_change_tracker_visible(tracker, now_ms)) {
        tracker->total = 0;
    }
    tracker->total += delta;
    tracker->last_change_ms = now_ms;
    tracker->active = true;
    return tracker->total;
}

bool puck_change_tracker_visible(puck_change_tracker_t *tracker, uint32_t now_ms)
{
    if (tracker->active && now_ms - tracker->last_change_ms >= tracker->show_ms) {
        tracker->active = false;
        tracker->total = 0;
    }
    return tracker->active;
}
