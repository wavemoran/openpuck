// The Colors swatch ring (per-player background and text color: ten swatches
// around a preview of the counter) and the custom picker panel (hue ring, tint
// slider, preview). The Custom ring between them is drawn by ui_menu.c.
#include "ui_internal.h"

#include "counter.h"
#include "puck_color.h"
#include "puck_touch_input.h"
#include "ui_theme.h"

#include <math.h>

LV_FONT_DECLARE(puck_font_small_32)

// Swatch ring (240x240 round screen)
#define RING_CENTER        120
#define SWATCH_COUNT       (PUCK_COLOR_PRESET_COUNT + 1)   // the presets, then Custom
#define SWATCH_SIZE        34
#define SWATCH_RADIUS      94     // swatch centers' circle
#define SWATCH_START_DEG   120.0f // first swatch; 0 = right, clockwise
#define SWATCH_STEP_DEG    (300.0f / (SWATCH_COUNT - 1))   // last one at 60: a gap at the bottom
#define SWATCH_OUTLINE     2      // around black swatches
#define MATCH_RING_W       2      // violet ring around the preset in use, at radius 21
#define MATCH_RING_GAP     2
#define CUSTOM_SEGMENTS    12     // hue wedges of the Custom swatch
#define PREVIEW_R          46
#define PREVIEW_RING_R     50     // violet ring around the preview
#define PREVIEW_RING_W     2
#define PREVIEW_SEAM_H     2
#define PREVIEW_SELECT_W   2      // violet outline of the selected half
#define PREVIEW_COUNT_DY   16     // "20" in a half, from the center
#define PREVIEW_TAG_DY     35     // "P1" / "P2" in the selected half
#define DONE_W             50
#define DONE_H             24
#define DONE_TOP           208
#define DONE_TOUCH_EXT     10     // 24 + 2 * 10 = 44 px touch height

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

#define PICK_PREVIEW_RING_W   2
#define PICK_PREVIEW_RING_GAP 2
#define KNOB_BORDER           2

static lv_obj_t *s_panel;
static lv_obj_t *s_swatch[PUCK_COLOR_PRESET_COUNT];
static lv_obj_t *s_preview_box;     // the center preview, rebuilt on every change
static uint8_t s_player;            // player being edited (0 = P1, also the 1-player color)

static lv_obj_t *s_picker;
static lv_obj_t *s_marker;
static lv_obj_t *s_preview;
static lv_obj_t *s_preview_text;
static bool s_hue_drag;             // a press started in the ring band
static bool s_edit_background;      // false: editing the text color
static uint16_t s_hue;
static uint8_t s_tint;

static void update_marks(void);

uint8_t ui_colors_player(void)
{
    return s_player;
}

// Apply a change through a copy of the stored settings (saves immediately).
static void store_colors(uint32_t background, uint32_t text)
{
    puck_settings_t copy = *counter_settings();
    copy.player_background[s_player] = background;
    copy.player_text[s_player] = text;
    counter_set_settings(&copy, lv_tick_get());
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
    ui_menu_refresh();   // the Custom ring's dots
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

void ui_colors_open_picker(bool background)
{
    if (s_picker) {
        return;
    }
    s_edit_background = background;
    s_hue_drag = false;
    const puck_settings_t *settings = counter_settings();
    const uint32_t current = background ? settings->player_background[s_player]
                                        : settings->player_text[s_player];
    puck_color_to_hue_tint(current, &s_hue, &s_tint);
    // Black, white and grays have no hue: starting at their tint (0 for black)
    // would make the hue ring look like it does nothing. Start mid-tint, at
    // the pure color, instead. Real colors open exactly as they are.
    const uint8_t r = (uint8_t)(current >> 16), g = (uint8_t)(current >> 8), b = (uint8_t)current;
    if (r == g && g == b) {
        s_tint = 50;
    }

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
    lv_obj_set_style_outline_width(s_preview, PICK_PREVIEW_RING_W, 0);
    lv_obj_set_style_outline_pad(s_preview, PICK_PREVIEW_RING_GAP, 0);
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

// ---- Swatch ring ----

static void close_panel(void)
{
    lv_obj_delete_async(s_panel);
    s_panel = NULL;
    s_preview_box = NULL;
    ui_counter_refresh(false, lv_tick_get());
}

// Done, or power: back to the Settings ring; false when it is not open.
bool ui_colors_close(void)
{
    if (!s_panel) {
        return false;
    }
    close_panel();
    ui_menu_open_settings();
    return true;
}

static void done_event(lv_event_t *e)
{
    ui_colors_close();
}

static bool two_players(void)
{
    return counter_game()->player_count == 2;
}

static void preset_event(lv_event_t *e)
{
    const puck_color_preset_t *preset = &PUCK_COLOR_PRESETS[(intptr_t)lv_event_get_user_data(e)];
    store_colors(preset->background, preset->text);
    if (two_players()) {
        // Stays open so the other player can be set too.
        update_marks();
    } else {
        // Straight back to the counter.
        close_panel();
    }
}

static void custom_event(lv_event_t *e)
{
    ui_menu_open_custom();
}

// Tapping a half of the 2 player preview selects that player.
static void preview_tap_event(lv_event_t *e)
{
    if (!two_players()) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    s_player = p.y < RING_CENTER ? 1 : 0;
    update_marks();
}

static lv_obj_t *add_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

// An arc-track wedge from start to end degrees in a size x size box.
static lv_obj_t *add_wedge(lv_obj_t *parent, int32_t size, uint16_t start, uint16_t end,
                           int32_t width, lv_color_t color)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_clickable(arc, false);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_arc_set_bg_angles(arc, start, end);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, color, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    return arc;
}

// "20" in a player's text color, upright or turned to face the other side.
static lv_obj_t *add_sample(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int32_t dy,
                            bool flip)
{
    lv_obj_t *label = ui_add_label(parent, font, "20");
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
    if (flip) {
        lv_obj_set_style_transform_pivot_x(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(label, 1800, 0);
    }
    return label;
}

// A small muted "P1"/"P2" in the selected half, toward that half's rim.
static void add_player_tag(uint8_t player, bool flip)
{
    lv_obj_t *tag = ui_add_label(s_preview_box, &lv_font_montserrat_14, player == 0 ? "P1" : "P2");
    lv_obj_set_style_text_color(tag, UI_COLOR_MUTED, 0);
    lv_obj_align(tag, LV_ALIGN_CENTER, 0, player == 0 ? PREVIEW_TAG_DY : -PREVIEW_TAG_DY);
    if (flip) {
        lv_obj_set_style_transform_pivot_x(tag, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(tag, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(tag, 1800, 0);
    }
}

// The center preview: P1's colors (1 player), or both halves split by the
// amber seam like the counter (2 players), the selected one outlined.
static void build_preview(void)
{
    lv_obj_clean(s_preview_box);
    const puck_settings_t *settings = counter_settings();
    const int32_t size = 2 * PREVIEW_R;
    if (!two_players()) {
        lv_obj_t *disc = add_plain(s_preview_box);
        lv_obj_set_size(disc, size, size);
        lv_obj_center(disc);
        lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(disc, lv_color_hex(settings->player_background[0]), 0);
        lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
        add_sample(s_preview_box, &puck_font_small_32, settings->player_text[0], 0, false);
        return;
    }
    // Half discs: a 180 degree arc as wide as the radius. Top is P2.
    add_wedge(s_preview_box, size, 180, 360, PREVIEW_R, lv_color_hex(settings->player_background[1]));
    add_wedge(s_preview_box, size, 0, 180, PREVIEW_R, lv_color_hex(settings->player_background[0]));
    const bool flip = !settings->p2_same_way;
    add_sample(s_preview_box, &puck_font_small_32, settings->player_text[1], -PREVIEW_COUNT_DY, flip);
    add_sample(s_preview_box, &puck_font_small_32, settings->player_text[0], PREVIEW_COUNT_DY, false);
    // The seam is the counter's: amber (allowed here as a copy of it).
    lv_obj_t *seam = add_plain(s_preview_box);
    lv_obj_set_size(seam, size, PREVIEW_SEAM_H);
    lv_obj_center(seam);
    lv_obj_set_style_bg_color(seam, UI_COLOR_AMBER, 0);
    lv_obj_set_style_bg_opa(seam, LV_OPA_COVER, 0);
    const bool top = s_player == 1;
    add_wedge(s_preview_box, size, top ? 180 : 0, top ? 360 : 180, PREVIEW_SELECT_W, UI_COLOR_VIOLET);
    add_player_tag(s_player, top && flip);
}

// Ring the preset matching the selected player's colors, and redraw the preview.
static void update_marks(void)
{
    if (!s_panel) {
        return;
    }
    const puck_settings_t *settings = counter_settings();
    for (int i = 0; i < PUCK_COLOR_PRESET_COUNT; i++) {
        const bool match = PUCK_COLOR_PRESETS[i].background == settings->player_background[s_player]
                        && PUCK_COLOR_PRESETS[i].text == settings->player_text[s_player];
        lv_obj_set_style_outline_width(s_swatch[i], match ? MATCH_RING_W : 0, 0);
    }
    build_preview();
}

// A swatch circle centered on the ring at index i; clickable.
static lv_obj_t *add_swatch_base(int i, lv_event_cb_t cb, void *user_data)
{
    const float rad = (SWATCH_START_DEG + SWATCH_STEP_DEG * (float)i) * ((float)M_PI / 180.0f);
    const int32_t x = (int32_t)lroundf(RING_CENTER + SWATCH_RADIUS * cosf(rad)) - SWATCH_SIZE / 2;
    const int32_t y = (int32_t)lroundf(RING_CENTER + SWATCH_RADIUS * sinf(rad)) - SWATCH_SIZE / 2;
    lv_obj_t *swatch = lv_obj_create(s_panel);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_scrollable(swatch, false);
    lv_obj_set_size(swatch, SWATCH_SIZE, SWATCH_SIZE);
    lv_obj_set_pos(swatch, x, y);
    lv_obj_set_style_radius(swatch, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(swatch, cb, LV_EVENT_CLICKED, user_data);
    return swatch;
}

static void add_preset_swatch(int index)
{
    const puck_color_preset_t *preset = &PUCK_COLOR_PRESETS[index];
    lv_obj_t *swatch = add_swatch_base(index, preset_event, (void *)(intptr_t)index);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(preset->background), 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    if (preset->background == PUCK_COLOR_BLACK) {
        lv_obj_set_style_border_width(swatch, SWATCH_OUTLINE, 0);
        lv_obj_set_style_border_color(swatch, UI_COLOR_LINE_STRONG, 0);
        lv_obj_set_style_border_opa(swatch, LV_OPA_COVER, 0);
    }
    lv_obj_set_style_outline_pad(swatch, MATCH_RING_GAP, 0);
    lv_obj_set_style_outline_color(swatch, UI_COLOR_VIOLET, 0);
    lv_obj_set_style_outline_opa(swatch, LV_OPA_COVER, 0);
    lv_obj_t *sample = ui_add_label(swatch, &lv_font_montserrat_14, "20");
    lv_obj_set_style_text_color(sample, lv_color_hex(preset->text), 0);
    lv_obj_center(sample);
    s_swatch[index] = swatch;
}

// Custom: hue wedges around a black "+".
static void add_custom_swatch(void)
{
    lv_obj_t *swatch = add_swatch_base(PUCK_COLOR_PRESET_COUNT, custom_event, NULL);
    const uint16_t step = 360 / CUSTOM_SEGMENTS;
    for (int i = 0; i < CUSTOM_SEGMENTS; i++) {
        add_wedge(swatch, SWATCH_SIZE, (uint16_t)(i * step), (uint16_t)((i + 1) * step), SWATCH_SIZE / 2,
                  lv_color_hex(puck_color_from_hue_tint((uint16_t)(i * step + step / 2), 50)));
    }
    lv_obj_t *plus = ui_add_label(swatch, &lv_font_montserrat_20, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(plus, lv_color_black(), 0);
    lv_obj_center(plus);
}

static void add_done(void)
{
    lv_obj_t *btn = lv_button_create(s_panel);
    ui_style_button(btn);
    lv_obj_set_size(btn, DONE_W, DONE_H);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, DONE_TOP);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_ext_click_area(btn, DONE_TOUCH_EXT);
    lv_obj_add_event_cb(btn, done_event, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_14, "Done"));
}

void ui_colors_open(void)
{
    if (s_panel) {
        return;
    }
    s_player = 0;
    s_panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_panel, UI_COLOR_BONE, 0);
    // Clickable so taps don't reach the counter underneath.
    lv_obj_set_clickable(s_panel, true);
    lv_obj_set_scrollable(s_panel, false);

    // Done first: the swatches next to it win where its touch area overlaps.
    add_done();
    for (int i = 0; i < PUCK_COLOR_PRESET_COUNT; i++) {
        add_preset_swatch(i);
    }
    add_custom_swatch();

    lv_obj_t *ring = add_plain(s_panel);
    lv_obj_set_size(ring, 2 * PREVIEW_RING_R, 2 * PREVIEW_RING_R);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, PREVIEW_RING_W, 0);
    lv_obj_set_style_border_color(ring, UI_COLOR_VIOLET_LINE, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);

    s_preview_box = add_plain(s_panel);
    lv_obj_set_size(s_preview_box, 2 * PREVIEW_R, 2 * PREVIEW_R);
    lv_obj_center(s_preview_box);
    lv_obj_set_clickable(s_preview_box, true);
    lv_obj_add_event_cb(s_preview_box, preview_tap_event, LV_EVENT_CLICKED, NULL);

    update_marks();
}
