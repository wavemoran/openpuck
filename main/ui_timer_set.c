// The timer length dial (opened from the Timer menu). Tapping the number or
// pressing power returns to the counter.
#include "ui_internal.h"

#include <stdio.h>

#include "counter.h"
#include "puck_settings.h"

// One-tap lengths: 55 for a best-of-three round, 75 for a longer round.
static const uint8_t PRESET_MINUTES[] = { 55, 75 };
_Static_assert(sizeof(PRESET_MINUTES) <= UI_DIAL_MAX_PRESETS, "too many timer presets");

static void read(ui_dial_state_t *state)
{
    const uint8_t minutes = counter_settings()->timer_minutes;
    snprintf(state->value, sizeof(state->value), "%u", (unsigned)minutes);
    snprintf(state->unit, sizeof(state->unit), "min");
    state->permille = (uint16_t)((minutes - PUCK_TIMER_MIN_MINUTES) * 1000
                                 / (PUCK_TIMER_MAX_MINUTES - PUCK_TIMER_MIN_MINUTES));
    state->at_min = minutes <= PUCK_TIMER_MIN_MINUTES;
    state->at_max = minutes >= PUCK_TIMER_MAX_MINUTES;
    state->preset = -1;
    for (size_t i = 0; i < sizeof(PRESET_MINUTES); i++) {
        if (minutes == PRESET_MINUTES[i]) {
            state->preset = (int8_t)i;
        }
    }
}

static void set_minutes(uint8_t minutes)
{
    puck_settings_t copy = *counter_settings();
    copy.timer_minutes = minutes;
    counter_set_settings(&copy, lv_tick_get());
}

// Applied right away (this also updates the running timer's duration).
static void step(int dir)
{
    set_minutes(puck_settings_step_timer(counter_settings()->timer_minutes, dir));
}

static void apply_preset(uint8_t index)
{
    set_minutes(PRESET_MINUTES[index]);
}

static void closed(void)
{
    ui_counter_refresh(false, lv_tick_get());
}

static const ui_dial_t TIMER_DIAL = {
    .title = "Timer",
    .presets = { "55", "75" },
    .preset_count = sizeof(PRESET_MINUTES),
    .apply_preset = apply_preset,
    .read = read,
    .step = step,
    .closed = closed,
};

void ui_timer_set_open(void)
{
    ui_dial_open(&TIMER_DIAL);
}
