// The quadrant menu: an opaque backdrop on the top layer split by a cross into
// four quadrants around a center circle. Each option's label curves along the
// rim of its quadrant, with an optional smaller value line (the current value
// or state) on its center side. Pressing a quadrant lights up its rim band;
// releasing inside the same quadrant picks the option. Tapping the center goes
// back or closes. The sub-rings (New game, Timer, Settings) reuse the layout;
// the Custom color ring uses it with two halves instead of four quadrants.
#include "ui.h"

#include <math.h>
#include <stdio.h>

#include "counter.h"
#include "esp_app_desc.h"
#include "logo.h"
#include "puck_touch_input.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define MENU_SIZE          240
#define MENU_RADIUS        120
#define MENU_LOGO_SIZE     56
#define MENU_CENTER_RING   46    // radius of the violet ring around the center
#define MENU_RING_WIDTH    2
#define MENU_LABEL_RADIUS  98    // label cap-height center circle
#define MENU_VALUE_RADIUS  78    // value line cap-height center circle, inside the label
#define MENU_LABEL_SPAN    80    // degrees available to one label (quadrants are 90)
#define MENU_BAND_WIDTH    34    // pressed highlight, reaching the screen edge
#define MENU_SEPARATOR_W   2

// Custom ring (two halves): a dot and a straight label per half.
#define CUSTOM_DOT_SIZE    22
#define CUSTOM_TOP_DOT_DY  (-87)
#define CUSTOM_TOP_TEXT_DY (-58)
#define CUSTOM_BOT_DOT_DY  64
#define CUSTOM_BOT_TEXT_DY 86
#define CUSTOM_FOR_DY      106

#define MENU_COLOR_TEXT      LOGO_COLOR_RING          // Bone
#define MENU_COLOR_VALUE     UI_COLOR_MUTED
#define MENU_COLOR_SEPARATOR UI_COLOR_LINE_STRONG
#define MENU_COLOR_PRESSED   UI_COLOR_VIOLET_PRESSED
#define MENU_COLOR_RING      UI_COLOR_VIOLET_LINE

#define ZONE_COUNT 5

typedef enum {
    ACTION_NONE,
    ACTION_CLOSE,
    // Rings (also the values of s_level).
    ACTION_MENU_MAIN,
    ACTION_MENU_NEW_GAME,
    ACTION_MENU_TIMER,
    ACTION_MENU_SETTINGS,
    ACTION_MENU_CUSTOM,
    // New game
    ACTION_NEW_1,
    ACTION_NEW_2,
    ACTION_NEW_RESET,
    ACTION_P2_FACES,
    // Timer
    ACTION_TIMER_TOGGLE,
    ACTION_TIMER_RESET,
    ACTION_TIMER_ENABLE,
    ACTION_TIMER_SET,
    // Main
    ACTION_HISTORY,
    // Settings
    ACTION_COLORS,
    ACTION_BRIGHTNESS,
    ACTION_AUTO_OFF,
    ACTION_BATTERY,
    // Custom
    ACTION_CUSTOM_BACKGROUND,
    ACTION_CUSTOM_TEXT,
} menu_action_t;

static lv_obj_t *s_menu;
static lv_obj_t *s_band;       // highlight arc behind the labels
static lv_obj_t *s_sep[4];     // separators at 0, 90, 180 and 270 degrees
static lv_obj_t *s_content;    // labels and center content of the current menu
static menu_action_t s_actions[ZONE_COUNT]; // action per puck_zone_t
static puck_zone_t s_down_zone;
static menu_action_t s_level;  // ring shown: one of the ACTION_MENU_* values

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
static void open_at(menu_action_t level);

bool ui_menu_back(void)
{
    if (!s_menu) {
        return false;
    }
    // The Custom ring sits over the Colors swatch ring: closing it goes back there.
    if (s_level == ACTION_MENU_MAIN || s_level == ACTION_MENU_CUSTOM) {
        ui_menu_close();
    } else {
        show_menu(ACTION_MENU_MAIN);
    }
    return true;
}

void ui_menu_open_settings(void)
{
    open_at(ACTION_MENU_SETTINGS);
}

void ui_menu_open_custom(void)
{
    open_at(ACTION_MENU_CUSTOM);
}

void ui_menu_refresh(void)
{
    if (s_menu) {
        show_menu(s_level);
    }
}

static void toggle_p2_same_way(uint32_t now)
{
    puck_settings_t copy = *counter_settings();
    copy.p2_same_way = !copy.p2_same_way;
    counter_set_settings(&copy, now);
    ui_counter_refresh(false, now);
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
    case ACTION_MENU_SETTINGS:
    case ACTION_MENU_CUSTOM:
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
    case ACTION_P2_FACES:
        // In place: the counter changes underneath, the ring stays open.
        toggle_p2_same_way(now);
        show_menu(s_level);
        return;
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
    case ACTION_COLORS:
        ui_menu_close();
        ui_colors_open();
        return;
    case ACTION_BRIGHTNESS:
        ui_menu_close();
        ui_settings_open_brightness();
        return;
    case ACTION_AUTO_OFF:
        ui_menu_close();
        ui_settings_open_auto_off();
        return;
    case ACTION_BATTERY:
        ui_settings_cycle_battery();
        show_menu(s_level);
        return;
    case ACTION_CUSTOM_BACKGROUND:
    case ACTION_CUSTOM_TEXT:
        // The picker opens above this ring and returns to it.
        ui_colors_open_picker(action == ACTION_CUSTOM_BACKGROUND);
        return;
    }
    ui_menu_close();
}

static bool two_zone(void)
{
    return s_level == ACTION_MENU_CUSTOM;
}

// The touched zone; on the two-zone ring each half is reported as its left
// quadrant, so a press can slide across the vertical center line.
static puck_zone_t zone_at_pointer(void)
{
    lv_point_t p = { 0, 0 };
    lv_indev_t *indev = lv_indev_active();
    if (indev) {
        lv_indev_get_point(indev, &p);
    }
    const puck_zone_t zone = puck_touch_zone(p.x, p.y, MENU_SIZE, MENU_SIZE);
    if (two_zone()) {
        if (zone == PUCK_ZONE_TOP_RIGHT) {
            return PUCK_ZONE_TOP_LEFT;
        }
        if (zone == PUCK_ZONE_BOTTOM_RIGHT) {
            return PUCK_ZONE_BOTTOM_LEFT;
        }
    }
    return zone;
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
    if (two_zone()) {
        const bool top = zone == PUCK_ZONE_TOP_LEFT;
        lv_arc_set_bg_angles(s_band, top ? 180 : 0, top ? 360 : 180);
    } else {
        lv_arc_set_bg_angles(s_band, QUADRANT[zone].band_start, QUADRANT[zone].band_end);
    }
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
//
// The value line uses the same drawing at a smaller radius and font, so it
// sits on the center side of the label in every quadrant.
#define LABEL_TEXT_MAX 16
#define VALUE_TEXT_MAX 16
#define LABEL_FONT     (&lv_font_montserrat_20)
#define VALUE_FONT     (&lv_font_montserrat_14)

static char s_label_text[ZONE_COUNT][LABEL_TEXT_MAX];
static char s_value_text[ZONE_COUNT][VALUE_TEXT_MAX];

// Width of the text in pixels along its baseline, kerning included.
static int32_t label_width(const lv_font_t *font, const char *text)
{
    int32_t width = 0;
    for (const char *c = text; *c; c++) {
        width += lv_font_get_glyph_width(font, (uint8_t)c[0], (uint8_t)c[1]);
    }
    return width;
}

// True when the text fits in a quadrant's label span at that radius. (Only
// used by asserts, which a release build may compile out.)
static inline bool fits_span(const lv_font_t *font, const char *text, int32_t radius)
{
    return label_width(font, text) * 180.0f <= (float)M_PI * MENU_LABEL_SPAN * radius;
}

static void draw_curved(lv_layer_t *layer, float cx, float cy, puck_zone_t zone, const char *text,
                        const lv_font_t *font, lv_color_t color, int32_t radius)
{
    const bool clockwise = QUADRANT[zone].clockwise;
    const float turn = clockwise ? 1.0f : -1.0f;

    // Put the capital height (not the line box) on the radius: the baseline is
    // half a cap height inside it for outward-facing glyphs and outside it for
    // inward-facing ones.
    lv_font_glyph_dsc_t cap;
    lv_font_get_glyph_dsc(font, &cap, 'H', 0);
    const float baseline_r = (float)radius - turn * (float)cap.box_h / 2;
    const float mid_deg = (float)(QUADRANT[zone].band_start + QUADRANT[zone].band_end) / 2;
    const int32_t total = label_width(font, text);

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
        dsc.color = color;
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

static void label_draw(lv_event_t *e)
{
    const puck_zone_t zone = (puck_zone_t)(intptr_t)lv_event_get_user_data(e);
    lv_obj_t *obj = lv_event_get_current_target_obj(e);
    lv_layer_t *layer = lv_event_get_layer(e);

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    const float cx = (float)area.x1 + (float)lv_area_get_width(&area) / 2;
    const float cy = (float)area.y1 + (float)lv_area_get_height(&area) / 2;

    draw_curved(layer, cx, cy, zone, s_label_text[zone], LABEL_FONT, MENU_COLOR_TEXT,
                MENU_LABEL_RADIUS);
    if (s_value_text[zone][0]) {
        draw_curved(layer, cx, cy, zone, s_value_text[zone], VALUE_FONT, MENU_COLOR_VALUE,
                    MENU_VALUE_RADIUS);
    }
}

// A quadrant option: its label and, unless value is NULL, a value line.
static void add_label(puck_zone_t zone, const char *text, const char *value, menu_action_t action)
{
    s_actions[zone] = action;
    snprintf(s_label_text[zone], LABEL_TEXT_MAX, "%s", text);
    snprintf(s_value_text[zone], VALUE_TEXT_MAX, "%s", value ? value : "");
    // The text must fit in its quadrant's arc at its radius.
    LV_ASSERT_MSG(fits_span(LABEL_FONT, s_label_text[zone], MENU_LABEL_RADIUS),
                  "menu label wider than its quadrant");
    LV_ASSERT_MSG(fits_span(VALUE_FONT, s_value_text[zone], MENU_VALUE_RADIUS),
                  "menu value line wider than its quadrant");

    lv_obj_t *label = lv_obj_create(s_content);
    lv_obj_remove_style_all(label);
    lv_obj_set_clickable(label, false);
    lv_obj_set_scrollable(label, false);
    lv_obj_set_size(label, MENU_SIZE, MENU_SIZE);
    lv_obj_set_pos(label, 0, 0);
    lv_obj_add_event_cb(label, label_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)zone);
}

// ---- Ring contents ----

static void add_new_game(void)
{
    const uint8_t players = counter_game()->player_count;
    add_label(PUCK_ZONE_TOP_LEFT, "Reset", players == 2 ? "keeps 2P" : "keeps 1P", ACTION_NEW_RESET);
    add_label(PUCK_ZONE_TOP_RIGHT, "P2 faces",
              counter_settings()->p2_same_way ? "same way" : "across", ACTION_P2_FACES);
    add_label(PUCK_ZONE_BOTTOM_LEFT, "1 player", players == 1 ? "playing now" : NULL, ACTION_NEW_1);
    add_label(PUCK_ZONE_BOTTOM_RIGHT, "2 players", players == 2 ? "playing now" : NULL, ACTION_NEW_2);
}

static void add_timer(void)
{
    const puck_settings_t *settings = counter_settings();
    char length[12];
    snprintf(length, sizeof(length), "%u min", (unsigned)settings->timer_minutes);
    if (!settings->timer_enabled) {
        add_label(PUCK_ZONE_TOP_LEFT, "Timer on", length, ACTION_TIMER_ENABLE);
        return;
    }
    // Start/Pause shows the time left when the ring opened (a snapshot).
    const uint32_t now = lv_tick_get();
    const puck_timer_t *timer = counter_timer();
    char time[PUCK_TIMER_TEXT_MAX];
    char left[PUCK_TIMER_TEXT_MAX + sizeof(" left")];
    puck_timer_format(puck_timer_remaining_ms(timer, now), time, sizeof(time));
    if (puck_timer_overtime(timer, now)) {
        snprintf(left, sizeof(left), "%s", time);
    } else {
        snprintf(left, sizeof(left), "%s left", time);
    }
    add_label(PUCK_ZONE_TOP_LEFT, puck_timer_running(timer) ? "Pause" : "Start", left,
              ACTION_TIMER_TOGGLE);
    add_label(PUCK_ZONE_TOP_RIGHT, "Reset", NULL, ACTION_TIMER_RESET);
    add_label(PUCK_ZONE_BOTTOM_LEFT, "Timer off", NULL, ACTION_TIMER_ENABLE);
    add_label(PUCK_ZONE_BOTTOM_RIGHT, length, "length", ACTION_TIMER_SET);
}

static void add_settings(void)
{
    const puck_settings_t *settings = counter_settings();
    char text[VALUE_TEXT_MAX];
    add_label(PUCK_ZONE_TOP_LEFT, "Colors", counter_game()->player_count == 2 ? "P1 / P2" : NULL,
              ACTION_COLORS);
    snprintf(text, sizeof(text), "%u%%", (unsigned)settings->brightness);
    add_label(PUCK_ZONE_TOP_RIGHT, "Brightness", text, ACTION_BRIGHTNESS);
    if (settings->auto_off_enabled) {
        snprintf(text, sizeof(text), "%u min", (unsigned)settings->auto_off_minutes);
    } else {
        snprintf(text, sizeof(text), "Off");
    }
    add_label(PUCK_ZONE_BOTTOM_LEFT, "Auto-off", text, ACTION_AUTO_OFF);
    add_label(PUCK_ZONE_BOTTOM_RIGHT, "Battery", ui_settings_battery_text(settings->battery_display),
              ACTION_BATTERY);
}

// A filled circle in a color, outlined so black and near-black still show.
static void add_dot(int32_t dy, uint32_t color)
{
    lv_obj_t *dot = lv_obj_create(s_content);
    lv_obj_remove_style_all(dot);
    lv_obj_set_clickable(dot, false);
    lv_obj_set_size(dot, CUSTOM_DOT_SIZE, CUSTOM_DOT_SIZE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(dot, 1, 0);
    lv_obj_set_style_border_color(dot, UI_COLOR_LINE_STRONG, 0);
    lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, dy);
}

static lv_obj_t *add_straight(const lv_font_t *font, const char *text, int32_t dy)
{
    lv_obj_t *label = ui_add_label(s_content, font, text);
    lv_obj_set_style_text_color(label, MENU_COLOR_TEXT, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, dy);
    return label;
}

// Custom color ring: Background on top, Text below, straight labels.
static void add_custom(void)
{
    const uint8_t player = ui_colors_player();
    const puck_settings_t *settings = counter_settings();
    s_actions[PUCK_ZONE_TOP_LEFT] = ACTION_CUSTOM_BACKGROUND;
    s_actions[PUCK_ZONE_BOTTOM_LEFT] = ACTION_CUSTOM_TEXT;
    add_dot(CUSTOM_TOP_DOT_DY, settings->player_background[player]);
    add_straight(&lv_font_montserrat_20, "Background", CUSTOM_TOP_TEXT_DY);
    add_dot(CUSTOM_BOT_DOT_DY, settings->player_text[player]);
    add_straight(&lv_font_montserrat_20, "Text", CUSTOM_BOT_TEXT_DY);
    lv_obj_t *who = add_straight(&lv_font_montserrat_14, player == 0 ? "for P1" : "for P2",
                                 CUSTOM_FOR_DY);
    lv_obj_set_style_text_color(who, MENU_COLOR_VALUE, 0);
}

// The center of a sub-ring: a violet chevron over "Back", plus the firmware
// version on the Settings ring.
static void add_back(bool version)
{
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
    if (version) {
        // For bug reports. version.txt holds a plain release number, so it
        // fits; clip with dots just in case.
        char text[40];
        snprintf(text, sizeof(text), "v%s", esp_app_get_description()->version);
        lv_obj_t *label = ui_add_label(box, &lv_font_montserrat_14, text);
        lv_obj_set_style_text_color(label, MENU_COLOR_VALUE, 0);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    }
    lv_obj_center(box);
}

static void show_menu(menu_action_t menu)
{
    if (s_content) {
        lv_obj_delete(s_content);
    }
    s_level = menu;
    s_content = lv_obj_create(s_menu);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_content, false);
    for (int i = 0; i < ZONE_COUNT; i++) {
        s_actions[i] = ACTION_NONE;
    }
    highlight(PUCK_ZONE_CENTER);
    // The vertical separators split quadrants; the two-zone ring has none.
    lv_obj_set_hidden(s_sep[1], two_zone());
    lv_obj_set_hidden(s_sep[3], two_zone());

    switch (menu) {
    case ACTION_MENU_NEW_GAME:
        add_new_game();
        break;
    case ACTION_MENU_TIMER:
        add_timer();
        break;
    case ACTION_MENU_SETTINGS:
        add_settings();
        break;
    case ACTION_MENU_CUSTOM:
        add_custom();
        break;
    default:
        add_label(PUCK_ZONE_TOP_LEFT, "New game", NULL, ACTION_MENU_NEW_GAME);
        add_label(PUCK_ZONE_TOP_RIGHT, "Timer", NULL, ACTION_MENU_TIMER);
        add_label(PUCK_ZONE_BOTTOM_RIGHT, "History", NULL, ACTION_HISTORY);
        add_label(PUCK_ZONE_BOTTOM_LEFT, "Settings", NULL, ACTION_MENU_SETTINGS);
        break;
    }

    // Tapping the center closes the main ring and the Custom ring (back to
    // the swatch ring underneath), and goes back to the main ring otherwise.
    const bool closes = menu == ACTION_MENU_MAIN || menu == ACTION_MENU_CUSTOM;
    s_actions[PUCK_ZONE_CENTER] = closes ? ACTION_CLOSE : ACTION_MENU_MAIN;
    if (menu == ACTION_MENU_MAIN) {
        lv_obj_center(logo_create(s_content, MENU_LOGO_SIZE));
    } else {
        add_back(menu == ACTION_MENU_SETTINGS);
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
        s_sep[i] = line;
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

static void open_at(menu_action_t level)
{
    if (s_menu) {
        show_menu(level);
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
    show_menu(level);
}

void ui_open_menu(void)
{
    if (s_menu) {
        return;
    }
    open_at(ACTION_MENU_MAIN);
}
