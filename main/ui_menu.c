// The quadrant menu: an opaque backdrop on the top layer split by a cross into
// four quadrants around a center circle. Each option's label curves along the
// rim of its quadrant. Pressing a quadrant lights up its rim band; releasing
// inside the same quadrant picks the option. Tapping the center goes back or
// closes. Sub-menus (new game, timer) reuse the layout with fewer labels.
#include "ui.h"

#include <math.h>
#include <stdio.h>

#include "counter.h"
#include "logo.h"
#include "puck_touch_input.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define MENU_SIZE          240
#define MENU_RADIUS        120
#define MENU_LOGO_SIZE     56
#define MENU_CENTER_RING   46    // radius of the violet ring around the center
#define MENU_RING_WIDTH    2
#define MENU_LABEL_RADIUS  98    // label baseline circle
#define MENU_LABEL_SPAN    80    // degrees available to one label (quadrants are 90)
#define MENU_BAND_WIDTH    34    // pressed highlight, reaching the screen edge
#define MENU_SEPARATOR_W   2

#define MENU_COLOR_TEXT      LOGO_COLOR_RING          // Bone
#define MENU_COLOR_SEPARATOR UI_COLOR_LINE_STRONG
#define MENU_COLOR_PRESSED   UI_COLOR_VIOLET_PRESSED
#define MENU_COLOR_RING      UI_COLOR_VIOLET_LINE

#define ZONE_COUNT 5

typedef enum {
    ACTION_NONE,
    ACTION_CLOSE,
    ACTION_MENU_MAIN,
    ACTION_MENU_NEW_GAME,
    ACTION_MENU_TIMER,
    ACTION_NEW_1,
    ACTION_NEW_2,
    ACTION_NEW_RESET,
    ACTION_TIMER_TOGGLE,
    ACTION_TIMER_RESET,
    ACTION_TIMER_ENABLE,
    ACTION_TIMER_SET,
    ACTION_HISTORY,
    ACTION_SETTINGS,
} menu_action_t;

static lv_obj_t *s_menu;
static lv_obj_t *s_band;       // highlight arc behind the labels
static lv_obj_t *s_content;    // labels and center content of the current menu
static menu_action_t s_actions[ZONE_COUNT]; // action per puck_zone_t
static puck_zone_t s_down_zone;
static menu_action_t s_level;  // ring shown: ACTION_MENU_MAIN / _NEW_GAME / _TIMER

// Each quadrant's 90 degrees (0 = right, 90 = bottom, clockwise on screen)
// and its direction, indexed by puck_zone_t.
static const struct {
    uint16_t band_start;
    uint16_t band_end;
    bool clockwise;
} QUADRANT[ZONE_COUNT] = {
    [PUCK_ZONE_TOP_LEFT]     = { 180, 270, true },
    [PUCK_ZONE_TOP_RIGHT]    = { 270, 360, true },
    [PUCK_ZONE_BOTTOM_RIGHT] = { 0, 90, false },
    [PUCK_ZONE_BOTTOM_LEFT]  = { 90, 180, false },
};

// Radial separators from the center ring to the rim, in the backdrop's
// coordinates, at 0, 90, 180 and 270 degrees.
#define SEP_IN  (MENU_RADIUS + MENU_CENTER_RING)
#define SEP_OUT (MENU_SIZE - 1)
static const lv_point_precise_t SEPARATORS[4][2] = {
    { { SEP_IN, MENU_RADIUS }, { SEP_OUT, MENU_RADIUS } },
    { { MENU_RADIUS, SEP_IN }, { MENU_RADIUS, SEP_OUT } },
    { { MENU_RADIUS - MENU_CENTER_RING, MENU_RADIUS }, { 0, MENU_RADIUS } },
    { { MENU_RADIUS, MENU_RADIUS - MENU_CENTER_RING }, { MENU_RADIUS, 0 } },
};

void ui_menu_close(void)
{
    if (s_menu) {
        lv_obj_delete_async(s_menu);
        s_menu = NULL;
        s_band = NULL;
        s_content = NULL;
        s_level = ACTION_MENU_MAIN;
    }
}

static void show_menu(menu_action_t menu);

bool ui_menu_back(void)
{
    if (!s_menu) {
        return false;
    }
    if (s_level == ACTION_MENU_MAIN) {
        ui_menu_close();
    } else {
        show_menu(ACTION_MENU_MAIN);
    }
    return true;
}

static void run_action(menu_action_t action)
{
    const uint32_t now = lv_tick_get();
    switch (action) {
    case ACTION_NONE:
        return;
    case ACTION_CLOSE:
        break;
    case ACTION_MENU_MAIN:
    case ACTION_MENU_NEW_GAME:
    case ACTION_MENU_TIMER:
        show_menu(action);
        return;
    case ACTION_NEW_1:
        counter_new_game(1, now);
        ui_counter_refresh(true, now);
        break;
    case ACTION_NEW_2:
        counter_new_game(2, now);
        ui_counter_refresh(true, now);
        break;
    case ACTION_NEW_RESET:
        counter_new_game(counter_game()->player_count, now);
        ui_counter_refresh(true, now);
        break;
    case ACTION_TIMER_TOGGLE:
        counter_timer_toggle(now);
        ui_counter_refresh(false, now);
        break;
    case ACTION_TIMER_RESET:
        counter_timer_reset(now);
        ui_counter_refresh(false, now);
        break;
    case ACTION_TIMER_ENABLE:
        counter_set_timer_enabled(!counter_settings()->timer_enabled, now);
        ui_counter_refresh(false, now);
        break;
    case ACTION_TIMER_SET:
        ui_menu_close();
        ui_timer_set_open();
        return;
    case ACTION_HISTORY:
        ui_menu_close();
        ui_history_open();
        return;
    case ACTION_SETTINGS:
        ui_menu_close();
        ui_settings_open();
        return;
    }
    ui_menu_close();
}

static puck_zone_t zone_at_pointer(void)
{
    lv_point_t p = { 0, 0 };
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    return puck_touch_zone(p.x, p.y, MENU_SIZE, MENU_SIZE);
}

static void highlight(puck_zone_t zone)
{
    if (!s_band) {
        return;
    }
    if (zone == PUCK_ZONE_CENTER || s_actions[zone] == ACTION_NONE) {
        lv_obj_set_hidden(s_band, true);
        return;
    }
    lv_arc_set_bg_angles(s_band, QUADRANT[zone].band_start, QUADRANT[zone].band_end);
    lv_obj_set_hidden(s_band, false);
}

static void backdrop_event(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_down_zone = zone_at_pointer();
        highlight(s_down_zone);
        break;
    case LV_EVENT_PRESSING:
        // Only the quadrant that was pressed stays lit while the finger is in it.
        highlight(zone_at_pointer() == s_down_zone ? s_down_zone : PUCK_ZONE_CENTER);
        break;
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        highlight(PUCK_ZONE_CENTER);
        break;
    case LV_EVENT_CLICKED:
        if (zone_at_pointer() == s_down_zone) {
            run_action(s_actions[s_down_zone]);
        }
        break;
    default:
        break;
    }
}

// Labels curve along the rim of their quadrant. They are drawn here letter by
// letter instead of with lv_arclabel: that widget spaces glyph centers by
// (prev width + width) / 2 where each width already includes the kerning to the
// following letter, so kerning is counted twice and letters after a strongly
// kerned one (the "r" in "Timer off") crowd together. Here the left edges
// advance by the kerned width and each glyph is centered on its own unkerned
// width, with its baseline center exactly on the circle.
//
// The top labels run clockwise (angle grows with reading order, glyph tops face
// outward). The bottom labels run counter-clockwise: the first letter is at the
// larger angle, i.e. on the left, so they still read left to right and the
// glyph tops face the center.
#define LABEL_TEXT_MAX 16

static char s_label_text[ZONE_COUNT][LABEL_TEXT_MAX];

// Width of the text in pixels along its baseline, kerning included.
static int32_t label_width(const lv_font_t *font, const char *text)
{
    int32_t width = 0;
    for (const char *c = text; *c; c++) {
        width += lv_font_get_glyph_width(font, (uint8_t)c[0], (uint8_t)c[1]);
    }
    return width;
}

static void label_draw(lv_event_t *e)
{
    const puck_zone_t zone = (puck_zone_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    const char *text = s_label_text[zone];
    const lv_font_t *font = &lv_font_montserrat_20;
    const bool clockwise = QUADRANT[zone].clockwise;
    const float turn = clockwise ? 1.0f : -1.0f;

    // Put the capital height (not the line box) on the label radius: the
    // baseline is half a cap height inside it for outward-facing glyphs and
    // outside it for inward-facing ones.
    lv_font_glyph_dsc_t cap;
    lv_font_get_glyph_dsc(font, &cap, 'H', 0);
    const float baseline_r = MENU_LABEL_RADIUS - turn * (float)cap.box_h / 2;
    const float mid_deg = (float)(QUADRANT[zone].band_start + QUADRANT[zone].band_end) / 2;
    const int32_t total = label_width(font, text);

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const float cx = (float)area.x1 + (float)lv_area_get_width(&area) / 2;
    const float cy = (float)area.y1 + (float)lv_area_get_height(&area) / 2;

    int32_t pos = 0; // pixels from the start of the text to this glyph's left edge
    for (const char *c = text; *c; c++) {
        const uint8_t letter = (uint8_t)c[0];
        const int32_t advance = lv_font_get_glyph_width(font, letter, 0);
        const float offset = (float)(pos + advance / 2) - (float)total / 2; // along the baseline
        const float deg = mid_deg + turn * offset / baseline_r * (180.0f / (float)M_PI);
        const float rad = deg * ((float)M_PI / 180.0f);
        pos += lv_font_get_glyph_width(font, letter, (uint8_t)c[1]);

        lv_draw_letter_dsc_t dsc;
        lv_draw_letter_dsc_init(&dsc);
        dsc.font = font;
        dsc.color = MENU_COLOR_TEXT;
        dsc.opa = LV_OPA_COVER;
        dsc.unicode = letter;
        dsc.rotation = (int32_t)((deg + turn * 90.0f) * 10);
        const lv_point_t point = {
            (int32_t)lroundf(cx + cosf(rad) * baseline_r),
            (int32_t)lroundf(cy + sinf(rad) * baseline_r),
        };
        lv_draw_letter(layer, &dsc, &point);
    }
}

static void add_label(puck_zone_t zone, const char *text, menu_action_t action)
{
    s_actions[zone] = action;
    snprintf(s_label_text[zone], LABEL_TEXT_MAX, "%s", text);
    // The text must fit in its quadrant's arc at the baseline radius.
    LV_ASSERT_MSG(label_width(&lv_font_montserrat_20, s_label_text[zone]) * 180.0f
                      <= (float)M_PI * MENU_LABEL_SPAN * MENU_LABEL_RADIUS,
                  "menu label wider than its quadrant");

    lv_obj_t *label = lv_obj_create(s_content);
    lv_obj_remove_style_all(label);
    lv_obj_set_clickable(label, false);
    lv_obj_set_scrollable(label, false);
    lv_obj_set_size(label, MENU_SIZE, MENU_SIZE);
    lv_obj_set_pos(label, 0, 0);
    lv_obj_add_event_cb(label, label_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)zone);
}

static void show_menu(menu_action_t menu)
{
    if (s_content) {
        lv_obj_delete(s_content);
    }
    s_level = menu == ACTION_MENU_NEW_GAME || menu == ACTION_MENU_TIMER ? menu : ACTION_MENU_MAIN;
    s_content = lv_obj_create(s_menu);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_content, false);
    for (int i = 0; i < ZONE_COUNT; i++) {
        s_actions[i] = ACTION_NONE;
    }
    highlight(PUCK_ZONE_CENTER);

    const bool running = puck_timer_running(counter_timer());
    const bool timer_enabled = counter_settings()->timer_enabled;
    switch (menu) {
    case ACTION_MENU_NEW_GAME:
        add_label(PUCK_ZONE_TOP_LEFT, "1 player", ACTION_NEW_1);
        add_label(PUCK_ZONE_TOP_RIGHT, "2 players", ACTION_NEW_2);
        add_label(PUCK_ZONE_BOTTOM_LEFT, "Reset", ACTION_NEW_RESET);
        break;
    case ACTION_MENU_TIMER:
        // Start/Pause and Reset only exist while the timer is enabled.
        if (timer_enabled) {
            add_label(PUCK_ZONE_TOP_LEFT, running ? "Pause" : "Start", ACTION_TIMER_TOGGLE);
            add_label(PUCK_ZONE_TOP_RIGHT, "Reset", ACTION_TIMER_RESET);
            char length[12];
            snprintf(length, sizeof(length), "%u min", (unsigned)counter_settings()->timer_minutes);
            add_label(PUCK_ZONE_BOTTOM_RIGHT, length, ACTION_TIMER_SET);
        }
        add_label(PUCK_ZONE_BOTTOM_LEFT, timer_enabled ? "Timer off" : "Timer on", ACTION_TIMER_ENABLE);
        break;
    default:
        add_label(PUCK_ZONE_TOP_LEFT, "New game", ACTION_MENU_NEW_GAME);
        add_label(PUCK_ZONE_TOP_RIGHT, "Timer", ACTION_MENU_TIMER);
        add_label(PUCK_ZONE_BOTTOM_RIGHT, "History", ACTION_HISTORY);
        add_label(PUCK_ZONE_BOTTOM_LEFT, "Settings", ACTION_SETTINGS);
        break;
    }

    // Tapping the center goes back (sub-menu) or closes (main menu).
    s_actions[PUCK_ZONE_CENTER] = menu == ACTION_MENU_MAIN ? ACTION_CLOSE : ACTION_MENU_MAIN;
    if (menu == ACTION_MENU_MAIN) {
        lv_obj_center(logo_create(s_content, MENU_LOGO_SIZE));
    } else {
        lv_obj_t *box = lv_obj_create(s_content);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, 2 * MENU_CENTER_RING - 8, LV_SIZE_CONTENT);
        lv_obj_set_clickable(box, false);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_text_color(box, MENU_COLOR_TEXT, 0);
        lv_obj_t *chevron = ui_add_label(box, &lv_font_montserrat_20, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(chevron, UI_COLOR_VIOLET, 0);
        ui_add_label(box, &lv_font_montserrat_14, "Back");
        lv_obj_center(box);
    }
}

static void add_static_decor(void)
{
    // Pressed-quadrant band: only the arc's background track is drawn.
    s_band = lv_arc_create(s_menu);
    lv_obj_set_clickable(s_band, false);
    lv_obj_set_size(s_band, MENU_SIZE, MENU_SIZE);
    lv_obj_set_pos(s_band, 0, 0);
    lv_obj_set_style_pad_all(s_band, 0, 0);
    lv_obj_set_style_arc_width(s_band, MENU_BAND_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_band, MENU_COLOR_PRESSED, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_band, false, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_band, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_band, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_band, 0, LV_PART_KNOB);
    lv_obj_set_hidden(s_band, true);

    for (int i = 0; i < 4; i++) {
        lv_obj_t *line = lv_line_create(s_menu);
        lv_obj_set_clickable(line, false);
        lv_line_set_points(line, SEPARATORS[i], 2);
        lv_obj_set_style_line_width(line, MENU_SEPARATOR_W, 0);
        lv_obj_set_style_line_color(line, MENU_COLOR_SEPARATOR, 0);
    }

    lv_obj_t *ring = lv_obj_create(s_menu);
    lv_obj_remove_style_all(ring);
    lv_obj_set_clickable(ring, false);
    lv_obj_set_size(ring, 2 * MENU_CENTER_RING, 2 * MENU_CENTER_RING);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, MENU_RING_WIDTH, 0);
    lv_obj_set_style_border_color(ring, MENU_COLOR_RING, 0);
    lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
    lv_obj_center(ring);
}

void ui_open_menu(void)
{
    if (s_menu) {
        return;
    }
    s_menu = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_menu);
    lv_obj_set_size(s_menu, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_menu, LV_OPA_COVER, 0);
    // Clickable so taps don't reach the counter underneath.
    lv_obj_set_clickable(s_menu, true);
    lv_obj_set_scrollable(s_menu, false);
    lv_obj_add_event_cb(s_menu, backdrop_event, LV_EVENT_ALL, NULL);
    s_down_zone = PUCK_ZONE_CENTER;

    add_static_decor();
    show_menu(ACTION_MENU_MAIN);
}
