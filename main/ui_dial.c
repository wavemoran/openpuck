// The value dial: one setting as a big value between round "-" and "+"
// buttons, with its title above, a unit line and range line below, and a
// progress arc around the rim. Holding a button repeats. Tapping the title
// (which has a back chevron) or the value, or pressing power, closes it. Used for the timer length, brightness and
// auto-off; each supplies its own read, step and close functions.
#include "ui_internal.h"

#include "ui_theme.h"

LV_FONT_DECLARE(puck_font_dial_64)

#define DIAL_SIZE         240
#define VALUE_TOUCH_SIZE  110    // tap area around the center
#define BUTTON_SIZE       52
#define BUTTON_DX         84     // reaches 110 (inside the arc) and leaves 116 px for "Off"
#define BUTTON_BORDER     2
#define UNIT_DY           40     // unit under the center
#define TITLE_DY          (-78)  // title row center: low enough to clear the arc
#define TITLE_TOUCH_W     150
#define TITLE_TOUCH_H     44
#define CHEVRON_GAP       6      // back chevron to the title
#define RANGE_DY          84     // range line near the bottom
#define ARC_SIZE          236    // rim arc: radius 118, 6 px wide (inner edge 112)
#define ARC_WIDTH         6
#define ARC_SCALE         1000   // arc values are permille

static lv_obj_t *s_panel;
static lv_obj_t *s_value;
static lv_obj_t *s_unit;
static lv_obj_t *s_arc;
static lv_obj_t *s_down;
static lv_obj_t *s_up;
static const ui_dial_t *s_dial;
static bool s_enabled[2];      // [0] "-", [1] "+"; false at that end of the range

// A button at the end of its range is dimmed and ignores taps. (Not
// LV_STATE_DISABLED: the theme's grey filter would replace this look.)
static void set_button_enabled(lv_obj_t *btn, int index, bool enabled)
{
    s_enabled[index] = enabled;
    lv_obj_set_style_bg_color(btn, enabled ? UI_COLOR_PANEL : UI_COLOR_DISABLED_FILL, 0);
    lv_obj_set_style_border_color(btn, enabled ? UI_COLOR_VIOLET_LINE : UI_COLOR_LINE, 0);
    lv_obj_set_style_text_color(btn, enabled ? UI_COLOR_BONE : UI_COLOR_LINE_STRONG, 0);
}

static void show(void)
{
    ui_dial_state_t state = { 0 };
    s_dial->read(&state);
    lv_label_set_text(s_value, state.value);
    lv_label_set_text(s_unit, state.unit);
    lv_arc_set_value(s_arc, state.permille > ARC_SCALE ? ARC_SCALE : state.permille);
    set_button_enabled(s_down, 0, !state.at_min);
    set_button_enabled(s_up, 1, !state.at_max);
}

bool ui_dial_close(void)
{
    if (!s_panel) {
        return false;
    }
    lv_obj_delete_async(s_panel);
    s_panel = NULL;
    const ui_dial_t *dial = s_dial;
    s_dial = NULL;
    dial->closed();
    return true;
}

static void close_event(lv_event_t *e)
{
    ui_dial_close();
}

// Tap, and the repeats while held: one step, applied right away.
static void step_event(lv_event_t *e)
{
    const int dir = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_enabled[dir > 0 ? 1 : 0]) {
        return;
    }
    s_dial->step(dir);
    show();
}

static lv_obj_t *add_step_button(int direction, const char *symbol)
{
    lv_obj_t *btn = lv_button_create(s_panel);
    lv_obj_set_size(btn, BUTTON_SIZE, BUTTON_SIZE);
    lv_obj_align(btn, LV_ALIGN_CENTER, direction * BUTTON_DX, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(btn, BUTTON_BORDER, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_CLICKED, (void *)(intptr_t)direction);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)direction);
    lv_obj_add_event_cb(btn, step_event, LV_EVENT_LONG_PRESSED_REPEAT, (void *)(intptr_t)direction);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_28, symbol));
    return btn;
}

// Progress around the rim: starts at the top, runs clockwise.
static void add_arc(void)
{
    s_arc = lv_arc_create(s_panel);
    lv_obj_set_clickable(s_arc, false);
    lv_obj_set_size(s_arc, ARC_SIZE, ARC_SIZE);
    lv_obj_center(s_arc);
    lv_obj_set_style_pad_all(s_arc, 0, 0);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_range(s_arc, 0, ARC_SCALE);
    lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, UI_COLOR_PANEL, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, UI_COLOR_VIOLET, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_arc, 0, LV_PART_KNOB);
}

void ui_dial_open(const ui_dial_t *dial)
{
    if (s_panel) {
        return;
    }
    s_dial = dial;
    s_panel = ui_panel_create(NULL);   // the panel's title sits too high for the arc

    add_arc();
    s_down = add_step_button(-1, "-");
    s_up = add_step_button(1, "+");

    lv_obj_t *number = lv_obj_create(s_panel);
    lv_obj_remove_style_all(number);
    lv_obj_set_size(number, VALUE_TOUCH_SIZE, VALUE_TOUCH_SIZE);
    lv_obj_set_style_radius(number, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_scrollable(number, false);
    lv_obj_align(number, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(number, close_event, LV_EVENT_CLICKED, NULL);
    s_value = ui_add_label(number, &puck_font_dial_64, "");
    lv_obj_set_clickable(s_value, false);
    lv_obj_center(s_value);

    // Title row: "< Title", a tap target that goes back. The chevron hangs off
    // the left of the centered title.
    lv_obj_t *back = lv_obj_create(s_panel);
    lv_obj_remove_style_all(back);
    lv_obj_set_scrollable(back, false);
    lv_obj_set_size(back, TITLE_TOUCH_W, TITLE_TOUCH_H);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, TITLE_DY);
    lv_obj_add_event_cb(back, close_event, LV_EVENT_CLICKED, NULL);
    lv_obj_t *title = ui_add_label(back, &lv_font_montserrat_20, dial->title);
    lv_obj_center(title);
    lv_obj_t *chevron = ui_add_label(back, &lv_font_montserrat_14, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(chevron, UI_COLOR_MUTED, 0);
    lv_obj_align_to(chevron, title, LV_ALIGN_OUT_LEFT_MID, -CHEVRON_GAP, 0);

    s_unit = ui_add_label(s_panel, &lv_font_montserrat_20, "");
    lv_obj_set_style_text_color(s_unit, UI_COLOR_MUTED, 0);
    lv_obj_align(s_unit, LV_ALIGN_CENTER, 0, UNIT_DY);

    lv_obj_t *range = ui_add_label(s_panel, &lv_font_montserrat_14, dial->range);
    lv_obj_set_style_text_color(range, UI_COLOR_MUTED, 0);
    lv_obj_align(range, LV_ALIGN_CENTER, 0, RANGE_DY);

    show();
}
