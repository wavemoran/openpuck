// The value dial: one setting as a big value between round "-" and "+"
// buttons, with its title above, a unit line and range line below, and a
// progress arc around the rim. Holding a button repeats. Tapping the title
// (which has a back chevron) or the value, or pressing power, closes it. Used for the timer length, brightness and
// auto-off; each supplies its own read, step and close functions. A dial can
// show preset pills in place of the range line (the timer's 55 and 75).
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
#define PRESET_DY         80     // pill row center
#define PRESET_W          56     // two pills and the gap stay inside the arc:
#define PRESET_H          30     //   outer end cap reaches 107 of the arc's 112
#define PRESET_GAP        10
#define PRESET_TOUCH_PAD  8      // extra touch area around each pill

static lv_obj_t *s_panel;
static lv_obj_t *s_value;
static lv_obj_t *s_unit;
static lv_obj_t *s_arc;
static lv_obj_t *s_down;
static lv_obj_t *s_up;
static const ui_dial_t *s_dial;
static bool s_enabled[2];      // [0] "-", [1] "+"; false at that end of the range
static lv_obj_t *s_presets[UI_DIAL_MAX_PRESETS];

// A button at the end of its range is dimmed and ignores taps. (Not
// LV_STATE_DISABLED: the theme's grey filter would replace this look.)
static void set_button_enabled(lv_obj_t *btn, int index, bool enabled)
{
    s_enabled[index] = enabled;
    lv_obj_set_style_bg_color(btn, enabled ? UI_COLOR_PANEL : UI_COLOR_DISABLED_FILL, 0);
    lv_obj_set_style_border_color(btn, enabled ? UI_COLOR_VIOLET_LINE : UI_COLOR_LINE, 0);
    lv_obj_set_style_text_color(btn, enabled ? UI_COLOR_BONE : UI_COLOR_LINE_STRONG, 0);
}

// The pill matching the value is filled violet; the others are outlined.
static void set_preset_selected(lv_obj_t *pill, bool selected)
{
    lv_obj_set_style_bg_color(pill, selected ? UI_COLOR_VIOLET : UI_COLOR_PANEL, 0);
    lv_obj_set_style_border_color(pill, selected ? UI_COLOR_VIOLET : UI_COLOR_VIOLET_LINE, 0);
    lv_obj_set_style_text_color(pill, selected ? UI_COLOR_VIOLET_INK : UI_COLOR_BONE, 0);
}

static void show(void)
{
    ui_dial_state_t state = { .preset = -1 };
    s_dial->read(&state);
    for (uint8_t i = 0; i < s_dial->preset_count; i++) {
        set_preset_selected(s_presets[i], state.preset == (int8_t)i);
    }
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

static void preset_event(lv_event_t *e)
{
    s_dial->apply_preset((uint8_t)(intptr_t)lv_event_get_user_data(e));
    show();
}

// The pill row near the bottom, centered.
static void add_presets(const ui_dial_t *dial)
{
    const int count = dial->preset_count;
    const int row_w = count * PRESET_W + (count - 1) * PRESET_GAP;
    for (int i = 0; i < count; i++) {
        lv_obj_t *pill = lv_button_create(s_panel);
        lv_obj_set_size(pill, PRESET_W, PRESET_H);
        lv_obj_align(pill, LV_ALIGN_CENTER,
                     -row_w / 2 + PRESET_W / 2 + i * (PRESET_W + PRESET_GAP), PRESET_DY);
        lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(pill, BUTTON_BORDER, 0);
        lv_obj_set_style_border_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(pill, 0, 0);
        lv_obj_set_style_pad_all(pill, 0, 0);
        lv_obj_set_ext_click_area(pill, PRESET_TOUCH_PAD);
        lv_obj_add_event_cb(pill, preset_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(ui_add_label(pill, &lv_font_montserrat_20, dial->presets[i]));
        s_presets[i] = pill;
    }
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

    if (dial->preset_count) {
        add_presets(dial);
    } else {
        lv_obj_t *range = ui_add_label(s_panel, &lv_font_montserrat_14, dial->range);
        lv_obj_set_style_text_color(range, UI_COLOR_MUTED, 0);
        lv_obj_align(range, LV_ALIGN_CENTER, 0, RANGE_DY);
    }

    show();
}
