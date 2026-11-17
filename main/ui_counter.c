// The counter screen: 1 or 2 player layout, touch handling, running changes,
// timer pill and battery indicator.
#include <string.h>

#include "battery_sense.h"
#include "counter.h"
#include "esp_lvgl_port.h"
#include "puck_battery.h"
#include "puck_color.h"
#include "puck_number.h"
#include "puck_touch_input.h"
#include "ui.h"
#include "ui_internal.h"
#include "ui_theme.h"

LV_FONT_DECLARE(puck_font_count_185)
LV_FONT_DECLARE(puck_font_count_80)
LV_FONT_DECLARE(puck_font_small_32)
LV_FONT_DECLARE(puck_font_timer_26)

#define CHANGE_SHOW_MS 1500
#define FLASH_OPA      89     // about 35% at the start of the tap flash
#define FLASH_OPA_CUSTOM LV_OPA_30   // on a colored background the flash is the text color
#define HINT_OPA       LV_OPA_60     // "-" / "+" on a colored background
#define FLASH_MS       200

// Offsets from the screen center (240x240 round), in pixels.
#define ONE_TOP_DY         (-94)  // battery group center in the top slot
#define ONE_CHANGE_DY      (-92)  // running change center, same slot as the battery
#define ONE_TIMER_DY       90     // timer pill center (y 210, spans 194-226; the underline ends at y 189)
#define ONE_COUNT_SPACE    (-10)  // letter spacing that keeps every 0-99 inside the circle
#define TWO_COUNT_DY       56     // count center per half, toward that player's rim
#define TWO_CHANGE_DY      106    // running change center, on the rim side of the count
#define TWO_SIGN_DX        78     // "-" and "+" from the center line
#define TWO_BATTERY_RIGHT  4      // battery group gap from the right screen edge
#define TWO_BATTERY_PAD    6      // black patch around the battery on the divider

// Underline for counts that read as another number upside down (6 and 9).
#define UNDERLINE_WIDTH_PCT 70
#define ONE_UNDERLINE_THICK 5
#define TWO_UNDERLINE_THICK 4
#define ONE_UNDERLINE_GAP   2
#define TWO_UNDERLINE_GAP   2

#define PILL_HEIGHT        32
#define PILL_BORDER        2
#define PILL_PAD_HOR       12
#define PILL_ICON_GAP      6      // time to pause icon
#define BATTERY_GAP        3      // symbol to percentage

typedef struct {
    lv_obj_t *count;
    lv_obj_t *underline;
    int32_t count_dy;       // count label center, from the screen center
    bool flip;              // faces the other way (player 2 of 2)
    lv_obj_t *change;
    puck_change_tracker_t tracker;
} player_view_t;

static lv_obj_t *s_root;
static lv_obj_t *s_pad;
static lv_obj_t *s_timer_pill;
static lv_obj_t *s_timer;
static lv_obj_t *s_timer_pause;
static lv_obj_t *s_battery;
static lv_obj_t *s_battery_icon;
static lv_obj_t *s_battery_pct;
static player_view_t s_view[PUCK_MAX_PLAYERS];
static lv_obj_t *s_hint[PUCK_MAX_PLAYERS][2];   // the "-" and "+" labels (2 players)
// Background rectangles: [0] top half, [1] bottom half (both player 0's color
// in the 1 player layout).
static lv_obj_t *s_bg[2];
// Tap flash rectangles behind the labels: [0] top half, [1] bottom half.
static lv_obj_t *s_flash[2];
static uint8_t s_built_players;

// Last values shown, so the labels are only touched when something changes.
static char s_timer_text[PUCK_TIMER_TEXT_MAX];
static bool s_timer_red;
static bool s_timer_paused;
static const char *s_battery_symbol;
static bool s_battery_red;
static bool s_battery_charging;
static int s_battery_percent;

static lv_color_t faint(void)
{
    return lv_palette_darken(LV_PALETTE_GREY, 2);
}

static void update_battery(void);

static uint32_t background_of(uint8_t player)
{
    return counter_settings()->player_background[player];
}

static lv_color_t text_of(uint8_t player)
{
    return lv_color_hex(counter_settings()->player_text[player]);
}

// Anything but a black background gets everything drawn in the player's text
// color, so nothing is unreadable (green on green). A black background keeps
// the default look (amber changes, gray hints) whatever the text color.
static bool is_custom(uint8_t player)
{
    return background_of(player) != PUCK_COLOR_BLACK;
}

// The timer pill and battery follow player 0 in the 1 player layout. On the
// 2 player divider they straddle both halves and keep the default look.
static bool chrome_custom(void)
{
    return s_built_players == 1 && is_custom(0);
}

static lv_color_t timer_color(bool overtime)
{
    return overtime ? UI_COLOR_OVERTIME : chrome_custom() ? text_of(0) : lv_color_white();
}

static lv_color_t pill_border_color(bool overtime)
{
    return overtime ? UI_COLOR_OVERTIME : chrome_custom() ? text_of(0) : UI_COLOR_VIOLET_LINE;
}

static lv_color_t battery_color(bool red, bool charging)
{
    if (charging && !chrome_custom()) {
        return lv_palette_main(LV_PALETTE_GREEN);
    }
    return red ? lv_palette_main(LV_PALETTE_RED) : chrome_custom() ? text_of(0) : faint();
}

// (Re)apply the players' colors to everything built. Safe to call any time.
static void apply_colors(void)
{
    for (int i = 0; i < 2; i++) {
        // Top half is player 1 in the 2 player layout.
        const uint8_t player = s_built_players == 2 && i == 0 ? 1 : 0;
        lv_obj_set_style_bg_color(s_bg[i], lv_color_hex(background_of(player)), 0);
    }
    for (uint8_t p = 0; p < s_built_players; p++) {
        lv_obj_set_style_text_color(s_view[p].count, text_of(p), 0);
        lv_obj_set_style_bg_color(s_view[p].underline, text_of(p), 0);
        if (s_built_players == 2) {
            for (int h = 0; h < 2; h++) {
                lv_obj_set_style_text_color(s_hint[p][h], is_custom(p) ? text_of(p) : faint(), 0);
                lv_obj_set_style_text_opa(s_hint[p][h], is_custom(p) ? HINT_OPA : LV_OPA_COVER, 0);
            }
        }
    }
    const bool custom = chrome_custom();
    s_timer_red = false;
    lv_obj_set_style_border_color(s_timer_pill, pill_border_color(false), 0);
    lv_obj_set_style_text_color(s_timer, timer_color(false), 0);
    lv_obj_set_style_text_color(s_timer_pause,
        custom ? text_of(0) : UI_COLOR_VIOLET, 0);
    s_battery_red = false;
    s_battery_charging = false;
    lv_obj_set_style_text_color(s_battery_icon, battery_color(false, false), 0);
    lv_obj_set_style_text_color(s_battery_pct, custom ? text_of(0) : faint(), 0);
}

// A small rounded bar under the count when it reads as another number upside
// down. It sits below the digits from the player's point of view, so on a
// flipped half it goes on the other side of the label center.
static void update_underline(uint8_t player)
{
    player_view_t *view = &s_view[player];
    const bool show = puck_number_rotation_ambiguous(counter_game()->life[player]);
    lv_obj_set_hidden(view->underline, !show);
    if (!show) {
        return;
    }
    const bool one = s_built_players == 1;
    const int32_t thick = one ? ONE_UNDERLINE_THICK : TWO_UNDERLINE_THICK;
    const int32_t gap = one ? ONE_UNDERLINE_GAP : TWO_UNDERLINE_GAP;
    const lv_font_t *font = lv_obj_get_style_text_font(view->count, LV_PART_MAIN);
    lv_obj_update_layout(view->count);
    // Digits sit on the baseline, base_line above the bottom of the line box,
    // but round ones (0, 3, 5, 6, 8) overshoot it slightly: measure the ink of
    // the digits shown so the gap looks the same under a 6 as under a 9.
    int32_t overshoot = 0;
    for (const char *c = lv_label_get_text(view->count); *c; c++) {
        lv_font_glyph_dsc_t dsc;
        if (lv_font_get_glyph_dsc(font, &dsc, (uint32_t)*c, 0) && -dsc.ofs_y > overshoot) {
            overshoot = -dsc.ofs_y;
        }
    }
    const int32_t ink_bottom = font->line_height / 2 - font->base_line + overshoot;
    const int32_t below = ink_bottom + gap + thick / 2;
    lv_obj_set_size(view->underline, lv_obj_get_width(view->count) * UNDERLINE_WIDTH_PCT / 100, thick);
    lv_obj_set_style_radius(view->underline, thick / 2, 0);
    lv_obj_align(view->underline, LV_ALIGN_CENTER, 0, view->count_dy + (view->flip ? -below : below));
}

static void show_count(uint8_t player)
{
    lv_label_set_text_fmt(s_view[player].count, "%d", counter_game()->life[player]);
    update_underline(player);
}

// The bar under a count, hidden until the number needs it.
static lv_obj_t *add_underline(void)
{
    lv_obj_t *bar = lv_obj_create(s_root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_clickable(bar, false);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_hidden(bar, true);
    return bar;
}

static void flash_opa(void *obj, int32_t opa)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)opa, 0);
}

// Tint a half green (+) or red (-), fading out; on a colored background flash
// the player's text color instead. A new tap restarts the fade.
static void flash(lv_obj_t *rect, uint8_t player, int16_t delta)
{
    const bool custom = is_custom(player);
    lv_obj_set_style_bg_color(rect,
        custom ? text_of(player) : lv_palette_main(delta >= 0 ? LV_PALETTE_GREEN : LV_PALETTE_RED), 0);
    lv_anim_delete(rect, flash_opa);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, rect);
    lv_anim_set_exec_cb(&anim, flash_opa);
    lv_anim_set_values(&anim, custom ? FLASH_OPA_CUSTOM : FLASH_OPA, LV_OPA_TRANSP);
    lv_anim_set_duration(&anim, FLASH_MS);
    lv_anim_start(&anim);
}

static void apply(uint8_t player, int16_t delta)
{
    const uint32_t now = lv_tick_get();
    counter_adjust(player, delta, now);
    // 1 player: top half for +, bottom for -. 2 players: that player's half
    // (player 1 is the top, player 0 the bottom).
    const bool top = counter_game()->player_count == 1 ? delta > 0 : player == 1;
    flash(s_flash[top ? 0 : 1], player, delta);
    show_count(player);
    player_view_t *view = &s_view[player];
    const int32_t change = puck_change_tracker_add(&view->tracker, delta, now);
    lv_label_set_text_fmt(view->change, "%+d", (int)change);
    // Amber for both signs on the default background (the sign says which way).
    lv_obj_set_style_text_color(view->change, is_custom(player) ? text_of(player) : UI_COLOR_AMBER, 0);
    lv_obj_set_hidden(view->change, false);
    update_battery();
    ui_tick_update(now);
}

static void pad_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    const bool long_press = code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT;
    if (code != LV_EVENT_SHORT_CLICKED && !long_press) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    // Holding the center opens the menu instead of the +-5 step.
    if (long_press && puck_touch_in_center(p.x, p.y, lv_obj_get_width(s_pad), lv_obj_get_height(s_pad))) {
        if (code == LV_EVENT_LONG_PRESSED) {
            ui_open_menu();
        }
        return;
    }
    uint8_t player;
    int16_t delta;
    if (puck_touch_target(p.x, p.y, lv_obj_get_width(s_pad), lv_obj_get_height(s_pad),
                          counter_game()->player_count, long_press, &player, &delta)) {
        apply(player, delta);
    }
}

// Label centered at (dx, dy) from the screen center. flip turns it to face the
// player on the other side; the pivot is its own center, so it stays put.
static lv_obj_t *add_label_at(const lv_font_t *font, const char *text, int32_t dx, int32_t dy,
                              bool flip)
{
    lv_obj_t *label = ui_add_label(s_root, font, text);
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
    if (flip) {
        // Rotate each label rather than a container: a half-screen render
        // layer doesn't fit in RAM.
        lv_obj_set_style_transform_pivot_x(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_pivot_y(label, LV_PCT(50), 0);
        lv_obj_set_style_transform_rotation(label, 1800, 0);
    }
    return label;
}

static lv_obj_t *add_change(const lv_font_t *font, int32_t dx, int32_t dy, bool flip)
{
    lv_obj_t *label = add_label_at(font, "", dx, dy, flip);
    lv_obj_set_hidden(label, true);
    return label;
}

// A flex row sized to its content and centered at dy from the screen center,
// so children appearing or disappearing keep it centered.
static lv_obj_t *add_row(int32_t dx, int32_t dy)
{
    lv_obj_t *row = lv_obj_create(s_root);
    lv_obj_remove_style_all(row);
    lv_obj_set_clickable(row, false);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_CENTER, dx, dy);
    return row;
}

// Timer in an outlined pill: the time, plus a pause icon while paused.
// black_fill covers the divider line behind it in the 2 player layout.
static void add_timer_pill(int32_t dy, bool black_fill)
{
    s_timer_pill = add_row(0, dy);
    lv_obj_set_height(s_timer_pill, PILL_HEIGHT);
    lv_obj_set_style_radius(s_timer_pill, PILL_HEIGHT / 2, 0);
    lv_obj_set_style_border_width(s_timer_pill, PILL_BORDER, 0);
    lv_obj_set_style_border_color(s_timer_pill, UI_COLOR_VIOLET_LINE, 0);
    lv_obj_set_style_pad_hor(s_timer_pill, PILL_PAD_HOR, 0);
    lv_obj_set_style_pad_column(s_timer_pill, PILL_ICON_GAP, 0);
    lv_obj_set_style_bg_color(s_timer_pill, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_timer_pill, black_fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

    s_timer = ui_add_label(s_timer_pill, &puck_font_timer_26, "");
    s_timer_pause = ui_add_label(s_timer_pill, &lv_font_montserrat_14, LV_SYMBOL_PAUSE);
    lv_obj_set_style_text_color(s_timer_pause, UI_COLOR_VIOLET, 0);
}

// Battery symbol plus the percentage, as one centered group.
static void add_battery_group(int32_t dx, int32_t dy)
{
    s_battery = add_row(dx, dy);
    lv_obj_set_style_pad_column(s_battery, BATTERY_GAP, 0);
    s_battery_icon = ui_add_label(s_battery, &lv_font_montserrat_14, "");
    s_battery_pct = ui_add_label(s_battery, &lv_font_montserrat_14, "");
    lv_obj_set_style_text_color(s_battery_pct, faint(), 0);
}

static void build_one_player(void)
{
    // Top half adds, bottom half subtracts.
    s_view[0].count = add_label_at(&puck_font_count_185, "", 0, 0, false);
    lv_obj_set_style_text_letter_space(s_view[0].count, ONE_COUNT_SPACE, 0);
    s_view[0].underline = add_underline();
    s_view[0].count_dy = 0;
    s_view[0].flip = false;
    // The change and the battery share the top slot.
    s_view[0].change = add_change(&puck_font_small_32, 0, ONE_CHANGE_DY, false);
    add_battery_group(0, ONE_TOP_DY);
    add_timer_pill(ONE_TIMER_DY, false);
}

static void build_half(uint8_t player, bool flip)
{
    // Player 0 is the bottom half; player 1 the top half, facing the other way.
    const int32_t side = flip ? -1 : 1;
    const int32_t half_dy = flip ? -1 : 1;
    s_view[player].count = add_label_at(&puck_font_count_80, "", 0, half_dy * TWO_COUNT_DY, flip);
    s_view[player].underline = add_underline();
    s_view[player].count_dy = half_dy * TWO_COUNT_DY;
    s_view[player].flip = flip;
    s_view[player].change = add_change(&puck_font_small_32, 0, half_dy * TWO_CHANGE_DY, flip);
    lv_obj_t *minus = add_label_at(&lv_font_montserrat_28, "-", -side * TWO_SIGN_DX,
                                   half_dy * TWO_COUNT_DY, flip);
    s_hint[player][0] = minus;
    lv_obj_t *plus = add_label_at(&lv_font_montserrat_28, "+", side * TWO_SIGN_DX,
                                  half_dy * TWO_COUNT_DY, flip);
    s_hint[player][1] = plus;
}

static void build_two_players(void)
{
    // The amber seam between the halves, behind the timer pill and battery.
    lv_obj_t *divider = lv_obj_create(s_root);
    lv_obj_remove_style_all(divider);
    lv_obj_set_clickable(divider, false);
    lv_obj_set_size(divider, LV_PCT(100), 2);
    lv_obj_set_style_bg_color(divider, UI_COLOR_AMBER, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_align(divider, LV_ALIGN_CENTER, 0, 0);

    build_half(0, false);
    build_half(1, true);

    // Both on the divider, upright for player 1, with black fill so the line
    // doesn't run through them. The battery sits against the right edge.
    add_timer_pill(0, true);
    add_battery_group(0, 0);
    lv_obj_align(s_battery, LV_ALIGN_RIGHT_MID, -TWO_BATTERY_RIGHT, 0);
    lv_obj_set_style_bg_color(s_battery, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_battery, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_battery, TWO_BATTERY_PAD, 0);
}

static void build(void)
{
    lv_obj_clean(s_root);
    s_built_players = counter_game()->player_count;

    // Full-screen touch pad underneath; labels aren't clickable, so touches
    // anywhere reach it.
    s_pad = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_pad);
    lv_obj_set_size(s_pad, LV_PCT(100), LV_PCT(100));
    lv_obj_add_event_cb(s_pad, pad_event, LV_EVENT_ALL, NULL);

    // Background rectangles behind the labels, one per half.
    for (int i = 0; i < 2; i++) {
        s_bg[i] = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_bg[i]);
        lv_obj_set_clickable(s_bg[i], false);
        lv_obj_set_size(s_bg[i], LV_PCT(100), LV_PCT(50));
        lv_obj_align(s_bg[i], i == 0 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(s_bg[i], LV_OPA_COVER, 0);
    }

    // Tap flash rectangles (plain, no layers), invisible until a tap.
    for (int i = 0; i < 2; i++) {
        s_flash[i] = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_flash[i]);
        lv_obj_set_clickable(s_flash[i], false);
        lv_obj_set_size(s_flash[i], LV_PCT(100), LV_PCT(50));
        lv_obj_align(s_flash[i], i == 0 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(s_flash[i], LV_OPA_TRANSP, 0);
    }

    if (s_built_players == 2) {
        build_two_players();
    } else {
        build_one_player();
    }

    for (uint8_t i = 0; i < PUCK_MAX_PLAYERS; i++) {
        puck_change_tracker_init(&s_view[i].tracker, CHANGE_SHOW_MS);
    }
    // Force the labels below to be set on the next update.
    s_timer_text[0] = '\0';
    s_timer_red = false;
    s_timer_paused = false;
    s_battery_symbol = NULL;
    s_battery_red = false;
    s_battery_charging = false;
    s_battery_percent = -1;
    lv_obj_set_hidden(s_timer_pill, true);
    lv_obj_set_hidden(s_timer_pause, true);
    lv_obj_set_hidden(s_battery_pct, true);
    lv_obj_set_hidden(s_battery, true);
    apply_colors();
    for (uint8_t i = 0; i < s_built_players; i++) {
        show_count(i);
    }
}

static void update_timer(uint32_t now_ms)
{
    const puck_timer_t *timer = counter_timer();
    // With the timer enabled the pill is always shown, idle included (full duration).
    const bool show = counter_settings()->timer_enabled;
    if (lv_obj_is_hidden(s_timer_pill) == show) {
        lv_obj_set_hidden(s_timer_pill, !show);
    }
    if (!show) {
        return;
    }
    const bool paused = timer->state != PUCK_TIMER_RUNNING;  // idle counts as stopped
    if (paused != s_timer_paused) {
        s_timer_paused = paused;
        lv_obj_set_hidden(s_timer_pause, !paused);
    }
    char text[PUCK_TIMER_TEXT_MAX];
    puck_timer_format(puck_timer_remaining_ms(timer, now_ms), text, sizeof(text));
    if (strcmp(text, s_timer_text) != 0) {
        strcpy(s_timer_text, text);
        lv_label_set_text(s_timer, text);
    }
    const bool red = !chrome_custom() && puck_timer_overtime(timer, now_ms);
    if (red != s_timer_red) {
        s_timer_red = red;
        lv_obj_set_style_text_color(s_timer, timer_color(red), 0);
        lv_obj_set_style_border_color(s_timer_pill, pill_border_color(red), 0);
    }
}

static const char *battery_symbol(int percent)
{
    return percent >= 88 ? LV_SYMBOL_BATTERY_FULL
         : percent >= 63 ? LV_SYMBOL_BATTERY_3
         : percent >= 38 ? LV_SYMBOL_BATTERY_2
         : percent >= 13 ? LV_SYMBOL_BATTERY_1
         : LV_SYMBOL_BATTERY_EMPTY;
}

// In the 1 player layout a visible running change takes the battery's slot.
static void update_battery(void)
{
    const puck_battery_display_t mode = (puck_battery_display_t)counter_settings()->battery_display;
    const int percent = battery_sense_percent();
    const bool change_shown = s_built_players == 1 && !lv_obj_is_hidden(s_view[0].change);
    const bool show = mode != PUCK_BATTERY_DISPLAY_OFF && percent >= 0 && !change_shown;
    if (lv_obj_is_hidden(s_battery) == show) {
        lv_obj_set_hidden(s_battery, !show);
    }
    if (!show) {
        return;
    }
    const char *symbol = battery_symbol(percent);
    const bool charging = battery_sense_charging();
    const bool red = !charging && !chrome_custom() && puck_battery_low((uint8_t)percent);
    if (symbol != s_battery_symbol || red != s_battery_red || charging != s_battery_charging) {
        s_battery_symbol = symbol;
        s_battery_red = red;
        s_battery_charging = charging;
        // Green may not show on a colored background, so add a bolt there instead.
        if (charging && chrome_custom()) {
            lv_label_set_text_fmt(s_battery_icon, "%s" LV_SYMBOL_CHARGE, symbol);
        } else {
            lv_label_set_text(s_battery_icon, symbol);
        }
        lv_obj_set_style_text_color(s_battery_icon, battery_color(red, charging), 0);
    }
    // On the 2-player divider a visible timer pill leaves room only for the icon.
    const bool timer_on_divider = s_built_players == 2 && !lv_obj_is_hidden(s_timer_pill);
    const bool show_percent = mode == PUCK_BATTERY_DISPLAY_ICON_PERCENT && !timer_on_divider;
    if (lv_obj_is_hidden(s_battery_pct) == show_percent) {
        lv_obj_set_hidden(s_battery_pct, !show_percent);
    }
    if (show_percent && percent != s_battery_percent) {
        s_battery_percent = percent;
        lv_label_set_text_fmt(s_battery_pct, "%d%%", percent);
    }
}

void ui_counter_start(lv_obj_t *screen)
{
    s_root = lv_obj_create(screen);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_clickable(s_root, false);
    build();
    ui_counter_tick(lv_tick_get());
}

void ui_counter_refresh(bool new_game, uint32_t now_ms)
{
    if (!s_root) {
        return;
    }
    if (counter_game()->player_count != s_built_players) {
        build();
    } else {
        apply_colors();
        for (uint8_t i = 0; i < s_built_players; i++) {
            show_count(i);
        }
    }
    if (new_game) {
        for (uint8_t i = 0; i < s_built_players; i++) {
            puck_change_tracker_init(&s_view[i].tracker, CHANGE_SHOW_MS);
            lv_obj_set_hidden(s_view[i].change, true);
        }
    }
    ui_counter_tick(now_ms);
}

void ui_counter_tick(uint32_t now_ms)
{
    if (!s_root) {
        return;
    }
    for (uint8_t i = 0; i < s_built_players; i++) {
        if (!puck_change_tracker_visible(&s_view[i].tracker, now_ms)) {
            lv_obj_set_hidden(s_view[i].change, true);
        }
    }
    update_timer(now_ms);
    update_battery();
    ui_tick_update(now_ms);
}

bool ui_counter_animating(uint32_t now_ms)
{
    if (!s_root) {
        return false;
    }
    for (uint8_t i = 0; i < s_built_players; i++) {
        if (puck_change_tracker_visible(&s_view[i].tracker, now_ms)) {
            return true;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (s_flash[i] && lv_anim_get(s_flash[i], flash_opa)) {
            return true;
        }
    }
    return false;
}

void ui_counter_stop(void)
{
    s_root = NULL;
}
