// The Set timer page (opened from the Timer menu): the countdown length as a
// big number between round "-" and "+" buttons. Holding a button repeats.
// Tapping the number closes the page.
#include "ui_internal.h"

#include <stdio.h>

#include "counter.h"
#include "puck_settings.h"
#include "ui_theme.h"

LV_FONT_DECLARE(puck_font_count_80)

#define NUMBER_TOUCH_SIZE 110    // tap area around the center
#define BUTTON_SIZE       64
#define BUTTON_DX         80     // button centers from the center; reaches 112 of the 120 px radius
#define UNIT_DY           46     // "min" under the center
#define HINT_DY           84     // range hint near the bottom
#define TITLE_TOP         14

#define BUTTON_BORDER     2

#define COLOR_BUTTON      UI_COLOR_PANEL
#define COLOR_UNIT        UI_COLOR_MUTED

static lv_obj_t *s_panel;
static lv_obj_t *s_value;
static lv_obj_t *s_down;
static lv_obj_t *s_up;

static void show_value(void)
{
    const uint8_t minutes = counter_settings()->timer_minutes;
    char text[8];
    snprintf(text, sizeof(text), "%u", (unsigned)minutes);
    lv_label_set_text(s_value, text);
    lv_obj_set_style_opa(s_down, minutes <= PUCK_TIMER_MIN_MINUTES ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(s_up, minutes >= PUCK_TIMER_MAX_MINUTES ? LV_OPA_40 : LV_OPA_COVER, 0);
}

// Closes the page and refreshes the counter; false when it is not open.
bool ui_timer_set_close(void)
{
    if (!s_panel) {
        return false;
    }
    lv_obj_delete_async(s_panel);
    s_panel = NULL;
    ui_counter_refresh(false, lv_tick_get());
    return true;
}

static void close_event(lv_event_t *e)
{
    ui_timer_set_close();
}

// Tap, and the repeats while held: step the length and apply it right away
// (this also updates the running timer's duration).
static void step_event(lv_event_t *e)
{
    const int direction = (int)(intptr_t)lv_event_get_user_data(e);
    puck_settings_t copy = *counter_settings();
    copy.timer_minutes = puck_settings_step_timer(copy.timer_minutes, direction);
    counter_set_settings(&copy, lv_tick_get());
    show_value();
}

static lv_obj_t *add_step_button(int direction, const char *symbol)
{
    lv_obj_t *btn = lv_button_create(s_panel);
    lv_obj_set_size(btn, BUTTON_SIZE, BUTTON_SIZE);
    lv_obj_align(btn, LV_ALIGN_CENTER, direction * BUTTON_DX, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, COLOR_BUTTON, 0);
    lv_obj_set_style_border_width(btn, BUTTON_BORDER, 0);
    lv_obj_set_style_border_color(btn, UI_COLOR_VIOLET_LINE, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, UI_COLOR_BONE, 0);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_CLICKED, (void *)(intptr_t)direction);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)direction);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)direction);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_28, symbol));
    return btn;
}

void ui_timer_set_open(void)
{
    if (s_panel) {
        return;
    }
    s_panel = ui_panel_create("Timer");

    s_down = add_step_button(-1, "-");
    s_up = add_step_button(1, "+");

    lv_obj_t *number = lv_obj_create(s_panel);
    lv_obj_remove_style_all(number);
    lv_obj_set_size(number, NUMBER_TOUCH_SIZE, NUMBER_TOUCH_SIZE);
    lv_obj_set_style_radius(number, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scrollable(number, false);
    lv_obj_align(number, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(number, close_event, LV_EVENT_CLICKED, NULL);
    s_value = ui_add_label(number, &puck_font_count_80, "");
    lv_obj_set_clickable(s_value, false);
    lv_obj_center(s_value);

    lv_obj_t *unit = ui_add_label(s_panel, &lv_font_montserrat_20, "min");
    lv_obj_set_style_text_color(unit, COLOR_UNIT, 0);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, UNIT_DY);

    char hint[24];
    snprintf(hint, sizeof(hint), "%d-%d min", PUCK_TIMER_MIN_MINUTES, PUCK_TIMER_MAX_MINUTES);
    lv_obj_t *range = ui_add_label(s_panel, &lv_font_montserrat_14, hint);
    lv_obj_set_style_text_color(range, COLOR_UNIT, 0);
    lv_obj_align(range, LV_ALIGN_CENTER, 0, HINT_DY);

    show_value();
}
