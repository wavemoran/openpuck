// Settings actions behind the Settings ring (drawn by ui_menu.c): the
// Brightness and Auto-off dials, and Battery cycling in place. Every change is
// applied and saved immediately.
#include "ui_internal.h"

#include <stdio.h>

#include "counter.h"
#include "puck_settings.h"

// Apply a change to a copy of the stored settings. On failure nothing is
// stored, so the next read shows the stored value again.
static void change_settings(void (*change)(puck_settings_t *, int), int dir)
{
    puck_settings_t copy = *counter_settings();
    change(&copy, dir);
    counter_set_settings(&copy, lv_tick_get());
}

// Both dials go back to the Settings ring.
static void back_to_settings(void)
{
    ui_menu_open_settings();
}

// ---- Brightness: the backlight follows each step live ----

static void brightness_read(ui_dial_state_t *state)
{
    const uint8_t percent = counter_settings()->brightness;
    snprintf(state->value, sizeof(state->value), "%u", (unsigned)percent);
    snprintf(state->unit, sizeof(state->unit), "%%");
    state->permille = (uint16_t)((percent - PUCK_BRIGHTNESS_MIN) * 1000
                                 / (PUCK_BRIGHTNESS_MAX - PUCK_BRIGHTNESS_MIN));
    state->at_min = percent <= PUCK_BRIGHTNESS_MIN;
    state->at_max = percent >= PUCK_BRIGHTNESS_MAX;
}

static void brightness_change(puck_settings_t *s, int dir)
{
    s->brightness = puck_settings_step_brightness(s->brightness, dir);
}

static void brightness_step(int dir)
{
    change_settings(brightness_change, dir);
}

static const ui_dial_t BRIGHTNESS_DIAL = {
    .title = "Brightness",
    .range = "10-100%",
    .read = brightness_read,
    .step = brightness_step,
    .closed = back_to_settings,
};

void ui_settings_open_brightness(void)
{
    ui_dial_open(&BRIGHTNESS_DIAL);
}

// ---- Auto-off: Off, then each minute choice ----

static void auto_off_read(ui_dial_state_t *state)
{
    const puck_settings_t *s = counter_settings();
    const uint8_t index = puck_settings_auto_off_dial_index(s->auto_off_enabled, s->auto_off_minutes);
    if (index == 0) {
        snprintf(state->value, sizeof(state->value), "Off");
        snprintf(state->unit, sizeof(state->unit), "stays on");
    } else {
        snprintf(state->value, sizeof(state->value), "%u", (unsigned)s->auto_off_minutes);
        snprintf(state->unit, sizeof(state->unit), "min idle");
    }
    state->permille = (uint16_t)(index * 1000 / (PUCK_AUTO_OFF_DIAL_STOPS - 1));
    state->at_min = index == 0;
    state->at_max = index == PUCK_AUTO_OFF_DIAL_STOPS - 1;
}

static void auto_off_change(puck_settings_t *s, int dir)
{
    puck_settings_step_auto_off_dial(s->auto_off_enabled, s->auto_off_minutes, dir,
                                     &s->auto_off_enabled, &s->auto_off_minutes);
}

static void auto_off_step(int dir)
{
    change_settings(auto_off_change, dir);
}

static const ui_dial_t AUTO_OFF_DIAL = {
    .title = "Auto-off",
    .range = "Off 1 2 5 10 ... 60",
    .read = auto_off_read,
    .step = auto_off_step,
    .closed = back_to_settings,
};

void ui_settings_open_auto_off(void)
{
    ui_dial_open(&AUTO_OFF_DIAL);
}

// ---- Battery: Off -> Icon -> Icon + % -> Off ----

const char *ui_settings_battery_text(uint8_t choice)
{
    switch (choice) {
    case PUCK_BATTERY_DISPLAY_ICON:
        return "Icon";
    case PUCK_BATTERY_DISPLAY_ICON_PERCENT:
        return "Icon + %";
    default:
        return "Off";
    }
}

static void battery_change(puck_settings_t *s, int dir)
{
    (void)dir;
    switch (s->battery_display) {
    case PUCK_BATTERY_DISPLAY_OFF:
        s->battery_display = PUCK_BATTERY_DISPLAY_ICON;
        break;
    case PUCK_BATTERY_DISPLAY_ICON:
        s->battery_display = PUCK_BATTERY_DISPLAY_ICON_PERCENT;
        break;
    default:
        s->battery_display = PUCK_BATTERY_DISPLAY_OFF;
        break;
    }
}

void ui_settings_cycle_battery(void)
{
    change_settings(battery_change, 0);
    ui_counter_refresh(false, lv_tick_get());
}
