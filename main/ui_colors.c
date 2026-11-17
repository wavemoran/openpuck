// The Colors screen (per-player background and text color: presets plus a
// custom picker) and the custom picker panel (hue ring, tint slider, preview).
#include "ui_internal.h"

#include "counter.h"
#include "puck_color.h"
#include "puck_touch_input.h"
#include "ui_theme.h"

LV_FONT_DECLARE(puck_font_small_32)

// Colors screen (same stacked list as Settings)
#define LIST_TOP        40
#define LIST_WIDTH      170
#define LIST_PAD_TOP    8
#define LIST_PAD_BOTTOM 50
#define BLOCK_GAP       12
#define PLAYER_BTN_W    82
#define PLAYER_BTN_H    44
#define PLAYER_GAP      6
#define PRESET_CELL_W   56     // three cells fit the list width
#define PRESET_SIZE     48
#define PRESET_ROW      3
#define CUSTOM_W        150
#define CUSTOM_H        56     // "Custom background" wraps to two lines at montserrat 20

// Picker (240x240 round screen)
#define PICK_SIZE       240
#define PICK_CENTER     120
#define RING_SEGMENTS   24
#define RING_STEP       (360 / RING_SEGMENTS)
#define RING_WIDTH      24
// A press that starts at least this far from the center starts a hue drag
// (and then follows the finger by angle until release). It is the farthest
// corner of the center controls plus 4 px: the Cancel button (56x22 at dy 66)
// reaches 81.9 px, the Tint row (about 128x20 incl. knob at dy 40) 81.2 px.
#define RING_TOUCH_R    86
#define MARKER_SIZE     12
#define MARKER_RADIUS   (PICK_CENTER - RING_WIDTH / 2)
#define LABEL_DY        (-78)  // "Background" / "Text"
#define PREVIEW_SIZE    88
#define PREVIEW_DY      (-20)  // preview center, y -64 to 24
#define TINT_ROW_DY     40
#define TINT_GAP        6
#define SLIDER_W        88
#define SLIDER_H        10
#define CANCEL_W        56
#define CANCEL_H        22
#define CANCEL_DY       66

_Static_assert((CANCEL_W / 2) * (CANCEL_W / 2) + (CANCEL_DY + CANCEL_H / 2) * (CANCEL_DY + CANCEL_H / 2)
                  <= (RING_TOUCH_R - 4) * (RING_TOUCH_R - 4),
              "Cancel must stay inside the hue band with 4 px margin");

#define MATCH_RING_W       2      // violet ring around the preset in use...
#define MATCH_RING_GAP     2      // ...this far outside it (fits the 56 px cell)
#define PREVIEW_RING_W     2
#define PREVIEW_RING_GAP   2
#define KNOB_BORDER        2

#define COLOR_OUTLINE      UI_COLOR_LINE_STRONG

static lv_obj_t *s_panel;
static lv_obj_t *s_player_btn[PUCK_MAX_PLAYERS];
static lv_obj_t *s_preset_circle[PUCK_COLOR_PRESET_COUNT];
static uint8_t s_player;            // player being edited (0 = P1, also the 1-player color)

static lv_obj_t *s_picker;
static lv_obj_t *s_marker;
static lv_obj_t *s_preview;
static lv_obj_t *s_preview_text;
static bool s_hue_drag;             // a press started in the ring band
static bool s_edit_background;      // false: editing the text color
static uint16_t s_hue;
static uint8_t s_tint;

// Apply a change through a copy of the stored settings (saves immediately).
static void store_colors(uint32_t background, uint32_t text)
{
    puck_settings_t copy = *counter_settings();
    copy.player_background[s_player] = background;
    copy.player_text[s_player] = text;
    counter_set_settings(&copy, lv_tick_get());
}

// Highlight the selected player and the preset matching that player's colors.
static void update_marks(void)
{
    const puck_settings_t *settings = counter_settings();
    for (uint8_t i = 0; i < PUCK_MAX_PLAYERS; i++) {
        const bool selected = i == s_player;
        lv_obj_set_style_bg_color(s_player_btn[i], selected ? UI_COLOR_VIOLET : UI_COLOR_PANEL, 0);
        lv_obj_set_style_text_color(s_player_btn[i], selected ? UI_COLOR_VIOLET_INK : UI_COLOR_BONE, 0);
        lv_obj_set_style_border_width(s_player_btn[i], selected ? 0 : 1, 0);
    }
    for (int i = 0; i < PUCK_COLOR_PRESET_COUNT; i++) {
        const bool match = PUCK_COLOR_PRESETS[i].background == settings->player_background[s_player]
                        && PUCK_COLOR_PRESETS[i].text == settings->player_text[s_player];
        lv_obj_set_style_border_width(s_preset_circle[i], 1, 0);
        lv_obj_set_style_border_color(s_preset_circle[i], COLOR_OUTLINE, 0);
        lv_obj_set_style_outline_width(s_preset_circle[i], match ? MATCH_RING_W : 0, 0);
    }
}

// Closes the Colors screen, back to Settings; false when it is not open.
bool ui_colors_close(void)
{
    if (!s_panel) {
        return false;
    }
    lv_obj_delete_async(s_panel);
    s_panel = NULL;
    return true;
}

static void done_event(lv_event_t *e)
{
    ui_colors_close();
    ui_counter_refresh(false, lv_tick_get());
}

static void player_event(lv_event_t *e)
{
    s_player = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    update_marks();
}

static void preset_event(lv_event_t *e)
{
    const puck_color_preset_t *preset = &PUCK_COLOR_PRESETS[(intptr_t)lv_event_get_user_data(e)];
    store_colors(preset->background, preset->text);
    // Straight back to the counter: close this screen and Settings.
    ui_colors_close();
    ui_settings_close();
}

// ---- Picker ----

static void update_picker(void)
{
    const uint32_t picked = puck_color_from_hue_tint(s_hue, s_tint);
    uint32_t background = counter_settings()->player_background[s_player];
    uint32_t text = picked;
    if (s_edit_background) {
        background = picked;
        text = puck_color_readable_text(picked);   // what tapping the preview saves
    }
    lv_obj_set_style_bg_color(s_preview, lv_color_hex(background), 0);
    lv_obj_set_style_text_color(s_preview_text, lv_color_hex(text), 0);

    const int32_t x = PICK_CENTER + (MARKER_RADIUS * (int32_t)lv_trigo_cos((int16_t)s_hue) >> LV_TRIGO_SHIFT);
    const int32_t y = PICK_CENTER + (MARKER_RADIUS * (int32_t)lv_trigo_sin((int16_t)s_hue) >> LV_TRIGO_SHIFT);
    lv_obj_set_pos(s_marker, x - MARKER_SIZE / 2, y - MARKER_SIZE / 2);
}

// Closes the custom picker without saving; false when it is not open.
bool ui_colors_picker_close(void)
{
    if (!s_picker) {
        return false;
    }
    lv_obj_delete_async(s_picker);
    s_picker = NULL;
    return true;
}

static void picker_touch_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_hue_drag = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    if (code == LV_EVENT_PRESSED) {
        // Only a press on the picker itself arrives here (the preview, slider
        // and Cancel take their own); it starts a drag if it lands in the band.
        const int32_t dx = p.x - PICK_CENTER;
        const int32_t dy = p.y - PICK_CENTER;
        s_hue_drag = dx * dx + dy * dy >= RING_TOUCH_R * RING_TOUCH_R;
    }
    if (!s_hue_drag) {
        return;
    }
    s_hue = puck_touch_angle(p.x, p.y, PICK_SIZE, PICK_SIZE);
    update_picker();
}

static void slider_event(lv_event_t *e)
{
    s_tint = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    update_picker();
}

static void preview_event(lv_event_t *e)
{
    const uint32_t picked = puck_color_from_hue_tint(s_hue, s_tint);
    if (s_edit_background) {
        store_colors(picked, puck_color_readable_text(picked));
    } else {
        store_colors(counter_settings()->player_background[s_player], picked);
    }
    ui_colors_picker_close();
    update_marks();
}

static void cancel_event(lv_event_t *e)
{
    ui_colors_picker_close();
}

static void add_ring(void)
{
    for (int i = 0; i < RING_SEGMENTS; i++) {
        lv_obj_t *arc = lv_arc_create(s_picker);
        lv_obj_set_clickable(arc, false);
        lv_obj_set_size(arc, PICK_SIZE, PICK_SIZE);
        lv_obj_set_pos(arc, 0, 0);
        lv_obj_set_style_pad_all(arc, 0, 0);
        lv_arc_set_bg_angles(arc, (uint16_t)(i * RING_STEP), (uint16_t)((i + 1) * RING_STEP));
        lv_obj_set_style_arc_width(arc, RING_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_color(arc,
            lv_color_hex(puck_color_from_hue_tint((uint16_t)(i * RING_STEP + RING_STEP / 2), 50)),
            LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    }
}

static void open_picker(bool background)
{
    if (s_picker) {
        return;
    }
    s_edit_background = background;
    s_hue_drag = false;
    const puck_settings_t *settings = counter_settings();
    puck_color_to_hue_tint(background ? settings->player_background[s_player]
                                      : settings->player_text[s_player], &s_hue, &s_tint);

    s_picker = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_picker);
    lv_obj_set_size(s_picker, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_picker, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_picker, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_picker, lv_color_white(), 0);
    // Clickable so taps don't reach the screens underneath; it also receives
    // the touches that pick the hue.
    lv_obj_set_clickable(s_picker, true);
    lv_obj_set_scrollable(s_picker, false);
    lv_obj_add_event_cb(s_picker, picker_touch_event, LV_EVENT_ALL, NULL);

    add_ring();

    s_marker = lv_obj_create(s_picker);
    lv_obj_remove_style_all(s_marker);
    lv_obj_set_clickable(s_marker, false);
    lv_obj_set_size(s_marker, MARKER_SIZE, MARKER_SIZE);
    lv_obj_set_style_radius(s_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_marker, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_marker, 1, 0);
    lv_obj_set_style_border_color(s_marker, lv_color_black(), 0);

    lv_obj_t *what = ui_add_label(s_picker, &lv_font_montserrat_14, background ? "Background" : "Text");
    lv_obj_align(what, LV_ALIGN_CENTER, 0, LABEL_DY);

    s_preview = lv_obj_create(s_picker);
    lv_obj_remove_style_all(s_preview);
    lv_obj_set_size(s_preview, PREVIEW_SIZE, PREVIEW_SIZE);
    lv_obj_align(s_preview, LV_ALIGN_CENTER, 0, PREVIEW_DY);
    lv_obj_set_style_radius(s_preview, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_width(s_preview, PREVIEW_RING_W, 0);
    lv_obj_set_style_outline_pad(s_preview, PREVIEW_RING_GAP, 0);
    lv_obj_set_style_outline_color(s_preview, UI_COLOR_VIOLET_LINE, 0);
    lv_obj_set_style_outline_opa(s_preview, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_preview, preview_event, LV_EVENT_CLICKED, NULL);
    s_preview_text = ui_add_label(s_preview, &puck_font_small_32, "20");
    lv_obj_center(s_preview_text);

    lv_obj_t *row = lv_obj_create(s_picker);
    lv_obj_remove_style_all(row);
    lv_obj_set_clickable(row, false);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, TINT_GAP, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, TINT_ROW_DY);
    ui_add_label(row, &lv_font_montserrat_14, "Tint");
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_size(slider, SLIDER_W, SLIDER_H);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, s_tint, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, UI_COLOR_VIOLET, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, UI_COLOR_BONE, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, KNOB_BORDER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, UI_COLOR_VIOLET, LV_PART_KNOB);
    lv_obj_set_style_border_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *cancel = lv_button_create(s_picker);
    ui_style_button(cancel);
    lv_obj_set_size(cancel, CANCEL_W, CANCEL_H);
    lv_obj_align(cancel, LV_ALIGN_CENTER, 0, CANCEL_DY);
    lv_obj_set_style_pad_all(cancel, 0, 0);
    lv_obj_add_event_cb(cancel, cancel_event, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_add_label(cancel, &lv_font_montserrat_14, "Cancel"));

    update_picker();
}

static void custom_event(lv_event_t *e)
{
    open_picker((bool)(intptr_t)lv_event_get_user_data(e));
}

// ---- Colors screen ----

static lv_obj_t *add_row(lv_obj_t *list)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    return row;
}

static void add_player_selector(lv_obj_t *list)
{
    lv_obj_t *row = add_row(list);
    lv_obj_set_style_pad_column(row, PLAYER_GAP, 0);
    for (uint8_t i = 0; i < PUCK_MAX_PLAYERS; i++) {
        lv_obj_t *btn = lv_button_create(row);
        ui_style_button(btn);
        lv_obj_set_size(btn, PLAYER_BTN_W, PLAYER_BTN_H);
        lv_obj_set_style_border_color(btn, UI_COLOR_LINE_STRONG, 0);
        lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(btn, player_event, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_center(ui_add_label(btn, &lv_font_montserrat_20, i == 0 ? "P1" : "P2"));
        s_player_btn[i] = btn;
    }
}

static void match_ring_space(lv_event_t *e)
{
    lv_event_set_ext_draw_size(e, MATCH_RING_GAP + MATCH_RING_W);
}

// The match ring reaches past the cell's top and bottom into the row gap:
// let the preset cell and its row draw (not lay out) that far outside.
static void show_match_ring(lv_obj_t *obj)
{
    lv_obj_set_overflow_visible(obj, true);
    lv_obj_add_event_cb(obj, match_ring_space, LV_EVENT_REFR_EXT_DRAW_SIZE, NULL);
    lv_obj_refresh_ext_draw_size(obj);
}

static void add_preset(lv_obj_t *row, int index)
{
    const puck_color_preset_t *preset = &PUCK_COLOR_PRESETS[index];
    lv_obj_t *cell = lv_obj_create(row);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, PRESET_CELL_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    show_match_ring(cell);
    show_match_ring(row);
    lv_obj_add_event_cb(cell, preset_event, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *circle = lv_obj_create(cell);
    lv_obj_remove_style_all(circle);
    lv_obj_set_clickable(circle, false);
    lv_obj_set_size(circle, PRESET_SIZE, PRESET_SIZE);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(preset->background), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_pad(circle, MATCH_RING_GAP, 0);
    lv_obj_set_style_outline_color(circle, UI_COLOR_VIOLET, 0);
    lv_obj_set_style_outline_opa(circle, LV_OPA_COVER, 0);
    lv_obj_t *sample = ui_add_label(circle, &puck_font_small_32, "20");
    lv_obj_set_style_text_color(sample, lv_color_hex(preset->text), 0);
    lv_obj_center(sample);
    s_preset_circle[index] = circle;
}

static void add_presets(lv_obj_t *list)
{
    lv_obj_t *row = NULL;
    for (int i = 0; i < PUCK_COLOR_PRESET_COUNT; i++) {
        if (i % PRESET_ROW == 0) {
            row = add_row(list);
        }
        add_preset(row, i);
    }
}

static void add_custom_button(lv_obj_t *list, const char *text, bool background)
{
    lv_obj_t *btn = lv_button_create(list);
    ui_style_button(btn);
    lv_obj_set_size(btn, CUSTOM_W, CUSTOM_H);
    lv_obj_add_event_cb(btn, custom_event, LV_EVENT_CLICKED, (void *)(intptr_t)background);
    lv_obj_t *label = ui_add_label(btn, &lv_font_montserrat_20, text);
    lv_obj_set_width(label, CUSTOM_W - 8);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

void ui_colors_open(void)
{
    if (s_panel) {
        return;
    }
    s_panel = ui_panel_create("Colors");
    s_player = 0;

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

    add_player_selector(list);
    add_presets(list);
    add_custom_button(list, "Custom background", true);
    add_custom_button(list, "Custom text", false);
    ui_add_done_button(list, done_event);

    update_marks();
}
