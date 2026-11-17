// The settings panel: a scrolling list of settings, each applied immediately,
// with a Done button at the end.
#include "ui_internal.h"

#include <stdio.h>

#include "counter.h"
#include "esp_app_desc.h"
#include "ui_theme.h"

#define LIST_TOP        40   // below the title
#define LIST_WIDTH      170   // the circle is ~178 px wide at the top of the list
#define LIST_PAD_TOP    8
#define LIST_PAD_BOTTOM 50   // lets the Done button scroll into the wide middle
#define BLOCK_GAP       16   // between settings
#define LABEL_GAP       6    // between a label and its control
#define STEP_BUTTON_W   44
#define STEP_BUTTON_H   44
#define STEP_VALUE_W    80   // "180 min" is 79 px at montserrat 20; 44 + 80 + 44 fits the list
#define CHOICE_W        120
#define CHOICE_H        44
#define COLORS_W        140

typedef enum {
    CTRL_AUTO_OFF_ENABLED,
    CTRL_AUTO_OFF_MINUTES_DOWN,
    CTRL_AUTO_OFF_MINUTES_UP,
    CTRL_BRIGHTNESS_DOWN,
    CTRL_BRIGHTNESS_UP,
    CTRL_TIMER_DOWN,
    CTRL_TIMER_UP,
    CTRL_BATTERY,
} control_t;

static lv_obj_t *s_panel;
static lv_obj_t *s_auto_off_switch;
static lv_obj_t *s_auto_off_group;    // the "Auto-off after" row, dimmed when off
static lv_obj_t *s_auto_off_down;
static lv_obj_t *s_auto_off_up;
static lv_obj_t *s_auto_off_value;
static lv_obj_t *s_brightness_value;
static lv_obj_t *s_timer_value;
static lv_obj_t *s_battery_text;

static void set_checked(lv_obj_t *sw, bool checked)
{
    if (checked) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_remove_state(sw, LV_STATE_CHECKED);
    }
}

static void set_enabled(lv_obj_t *obj, bool enabled)
{
    if (enabled) {
        lv_obj_remove_state(obj, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(obj, LV_STATE_DISABLED);
    }
}

static const char *battery_choice_text(uint8_t choice)
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

// Show the stored settings in the controls (also puts back a rejected change).
static void show_settings(void)
{
    const puck_settings_t *settings = counter_settings();
    char text[16];
    set_checked(s_auto_off_switch, settings->auto_off_enabled);
    snprintf(text, sizeof(text), "%u min", (unsigned)settings->auto_off_minutes);
    lv_label_set_text(s_auto_off_value, text);
    lv_obj_set_style_opa(s_auto_off_group, settings->auto_off_enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    set_enabled(s_auto_off_down, settings->auto_off_enabled);
    set_enabled(s_auto_off_up, settings->auto_off_enabled);
    snprintf(text, sizeof(text), "%u%%", (unsigned)settings->brightness);
    lv_label_set_text(s_brightness_value, text);
    snprintf(text, sizeof(text), "%u min", (unsigned)settings->timer_minutes);
    lv_label_set_text(s_timer_value, text);
    lv_label_set_text(s_battery_text, battery_choice_text(settings->battery_display));
}

static void control_event(lv_event_t *e)
{
    const control_t control = (control_t)(intptr_t)lv_event_get_user_data(e);
    puck_settings_t copy = *counter_settings();
    switch (control) {
    case CTRL_AUTO_OFF_ENABLED:
        copy.auto_off_enabled = lv_obj_has_state(s_auto_off_switch, LV_STATE_CHECKED);
        break;
    case CTRL_AUTO_OFF_MINUTES_DOWN:
        copy.auto_off_minutes = puck_settings_step_auto_off(copy.auto_off_minutes, -1);
        break;
    case CTRL_AUTO_OFF_MINUTES_UP:
        copy.auto_off_minutes = puck_settings_step_auto_off(copy.auto_off_minutes, 1);
        break;
    case CTRL_BRIGHTNESS_DOWN:
        copy.brightness = puck_settings_step_brightness(copy.brightness, -1);
        break;
    case CTRL_BRIGHTNESS_UP:
        copy.brightness = puck_settings_step_brightness(copy.brightness, 1);
        break;
    case CTRL_TIMER_DOWN:
        copy.timer_minutes = puck_settings_step_timer(copy.timer_minutes, -1);
        break;
    case CTRL_TIMER_UP:
        copy.timer_minutes = puck_settings_step_timer(copy.timer_minutes, 1);
        break;
    case CTRL_BATTERY:
        switch (copy.battery_display) {
        case PUCK_BATTERY_DISPLAY_OFF:
            copy.battery_display = PUCK_BATTERY_DISPLAY_ICON;
            break;
        case PUCK_BATTERY_DISPLAY_ICON:
            copy.battery_display = PUCK_BATTERY_DISPLAY_ICON_PERCENT;
            break;
        default:
            copy.battery_display = PUCK_BATTERY_DISPLAY_OFF;
            break;
        }
        break;
    }
    // On failure nothing is stored, so showing the stored settings undoes it.
    counter_set_settings(&copy, lv_tick_get());
    show_settings();
}

// One setting: a column holding the label on its own line, centered. The
// control goes below it.
static lv_obj_t *add_block(lv_obj_t *list, const char *text)
{
    lv_obj_t *block = lv_obj_create(list);
    lv_obj_remove_style_all(block);
    lv_obj_set_size(block, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(block, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(block, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(block, LABEL_GAP, 0);
    lv_obj_t *label = ui_add_label(block, &lv_font_montserrat_20, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return block;
}

static lv_obj_t *add_switch(lv_obj_t *list, const char *text, control_t control)
{
    lv_obj_t *block = add_block(list, text);
    lv_obj_t *sw = lv_switch_create(block);
    lv_obj_set_style_bg_color(sw, UI_COLOR_VIOLET, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, control_event, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)control);
    return sw;
}

static lv_obj_t *add_step_button(lv_obj_t *row, const char *text, control_t control)
{
    lv_obj_t *btn = lv_button_create(row);
    ui_style_button(btn);
    lv_obj_set_size(btn, STEP_BUTTON_W, STEP_BUTTON_H);
    lv_obj_add_event_cb(btn, control_event, LV_EVENT_CLICKED, (void *)(intptr_t)control);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_20, text));
    return btn;
}

// A label above a "-" / value / "+" row. Returns the block; the buttons and
// the value label come back through the out parameters.
static lv_obj_t *add_stepper(lv_obj_t *list, const char *text, control_t down, control_t up,
                             lv_obj_t **down_btn, lv_obj_t **up_btn, lv_obj_t **value)
{
    lv_obj_t *block = add_block(list, text);
    lv_obj_t *row = lv_obj_create(block);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    *down_btn = add_step_button(row, "-", down);
    *value = ui_add_label(row, &lv_font_montserrat_20, "");
    lv_obj_set_width(*value, STEP_VALUE_W);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
    *up_btn = add_step_button(row, "+", up);
    return block;
}

// A label above one button that shows the current choice and cycles on a tap.
static lv_obj_t *add_choice(lv_obj_t *list, const char *text, control_t control,
                            lv_obj_t **value)
{
    lv_obj_t *block = add_block(list, text);
    lv_obj_t *btn = lv_button_create(block);
    ui_style_button(btn);
    lv_obj_set_size(btn, CHOICE_W, CHOICE_H);
    lv_obj_add_event_cb(btn, control_event, LV_EVENT_CLICKED, (void *)(intptr_t)control);
    *value = ui_add_label(btn, &lv_font_montserrat_20, "");
    lv_obj_set_width(*value, CHOICE_W - 8);
    lv_label_set_long_mode(*value, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(*value);
    return block;
}

// Closes the Settings screen and refreshes the counter; a color preset uses it
// to return straight to the counter.
bool ui_settings_close(void)
{
    if (!s_panel) {
        return false;
    }
    lv_obj_delete_async(s_panel);
    s_panel = NULL;
    ui_counter_refresh(false, lv_tick_get());
    return true;
}

static void done_event(lv_event_t *e)
{
    ui_settings_close();
}

static void colors_event(lv_event_t *e)
{
    ui_colors_open();
}

// A button that opens the Colors screen.
static void add_colors_button(lv_obj_t *list)
{
    lv_obj_t *btn = lv_button_create(list);
    ui_style_button(btn);
    lv_obj_set_size(btn, COLORS_W, CHOICE_H);
    lv_obj_add_event_cb(btn, colors_event, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_20, "Colors"));
}

void ui_settings_open(void)
{
    if (s_panel) {
        return;
    }
    s_panel = ui_panel_create("Settings");
    ui_panel_add_underline(s_panel);

    lv_obj_t *list = lv_obj_create(s_panel);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LIST_WIDTH, 240 - LIST_TOP);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, LIST_TOP);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(list, LIST_PAD_TOP, 0);
    lv_obj_set_style_pad_bottom(list, LIST_PAD_BOTTOM, 0);
    lv_obj_set_style_pad_row(list, BLOCK_GAP, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    add_colors_button(list);
    lv_obj_t *down;
    lv_obj_t *up;
    add_stepper(list, "Brightness", CTRL_BRIGHTNESS_DOWN, CTRL_BRIGHTNESS_UP, &down, &up,
                &s_brightness_value);
    add_choice(list, "Battery", CTRL_BATTERY, &s_battery_text);
    add_stepper(list, "Timer length", CTRL_TIMER_DOWN, CTRL_TIMER_UP, &down, &up, &s_timer_value);
    s_auto_off_switch = add_switch(list, "Auto-off", CTRL_AUTO_OFF_ENABLED);
    s_auto_off_group = add_stepper(list, "Auto-off after", CTRL_AUTO_OFF_MINUTES_DOWN,
                                   CTRL_AUTO_OFF_MINUTES_UP, &s_auto_off_down, &s_auto_off_up,
                                   &s_auto_off_value);
    ui_add_done_button(list, done_event);

    // The firmware version, for bug reports.
    char version[48];
    snprintf(version, sizeof(version), "OpenPuck v%s", esp_app_get_description()->version);
    lv_obj_t *version_label = ui_add_label(list, &lv_font_montserrat_14, version);
    lv_obj_set_style_text_color(version_label, UI_COLOR_MUTED, 0);

    show_settings();
}
