// The timer length dial (opened from the Timer menu). Tapping the number or
// pressing power returns to the counter.
#include "ui_internal.h"

#include <stdio.h>

#include "counter.h"
#include "puck_settings.h"

static void read(ui_dial_state_t *state)
{
    const uint8_t minutes = counter_settings()->timer_minutes;
    snprintf(state->value, sizeof(state->value), "%u", (unsigned)minutes);
    snprintf(state->unit, sizeof(state->unit), "min");
    state->permille = (uint16_t)((minutes - PUCK_TIMER_MIN_MINUTES) * 1000
                                 / (PUCK_TIMER_MAX_MINUTES - PUCK_TIMER_MIN_MINUTES));
    state->at_min = minutes <= PUCK_TIMER_MIN_MINUTES;
    state->at_max = minutes >= PUCK_TIMER_MAX_MINUTES;
}

// Applied right away (this also updates the running timer's duration).
static void step(int dir)
{
    puck_settings_t copy = *counter_settings();
    copy.timer_minutes = puck_settings_step_timer(copy.timer_minutes, dir);
    counter_set_settings(&copy, lv_tick_get());
}

static void closed(void)
{
    ui_counter_refresh(false, lv_tick_get());
}

#define STR_(x) #x
#define STR(x)  STR_(x)

static const ui_dial_t TIMER_DIAL = {
    .title = "Timer",
    .range = STR(PUCK_TIMER_MIN_MINUTES) "-" STR(PUCK_TIMER_MAX_MINUTES) " min",
    .read = read,
    .step = step,
    .closed = closed,
};

void ui_timer_set_open(void)
{
    ui_dial_open(&TIMER_DIAL);
}
