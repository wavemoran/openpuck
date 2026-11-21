#include "ui.h"

#include "counter.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "logo.h"
#include "ui_internal.h"
#include "ui_theme.h"

#define TICK_FAST_MS         100   // a running change is visible or a tap flash is fading
#define TICK_MEDIUM_MS       250   // game timer running or a save pending
#define TICK_IDLE_MS         2000  // battery icon and charging state can wait
#define LONG_PRESS_MS        500
#define LONG_PRESS_REPEAT_MS 400
#define SPLASH_LOGO_SIZE     192
#define SPLASH_HOLD_MS       1000
#define SPLASH_FADE_MS       250
#define PANEL_TITLE_TOP      12   // history and settings: title distance from the top
#define PANEL_DONE_WIDTH     120
#define PANEL_BUTTON_HEIGHT  44
#define PANEL_UNDERLINE_W    40
#define PANEL_UNDERLINE_H    2
#define PANEL_UNDERLINE_Y    36   // in the gap between the title (ends at 34) and the list (40)

static lv_timer_t *s_tick_timer;
static uint32_t s_tick_period_ms = TICK_FAST_MS;

void ui_tick_update(uint32_t now_ms)
{
    if (!s_tick_timer) {
        return;
    }
    const uint32_t period = ui_counter_animating(now_ms) ? TICK_FAST_MS
        : (counter_timer_running() || counter_save_pending()) ? TICK_MEDIUM_MS
        : TICK_IDLE_MS;
    if (period != s_tick_period_ms) {
        s_tick_period_ms = period;
        lv_timer_set_period(s_tick_timer, period);
    }
}

// ui_counter_tick re-picks the period.
static void tick(lv_timer_t *timer)
{
    const uint32_t now = lv_tick_get();
    counter_poll(now);
    ui_counter_tick(now);
}

lv_obj_t *ui_add_label(lv_obj_t *parent, const lv_font_t *font, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, text);
    return label;
}

lv_obj_t *ui_panel_create(const char *title)
{
    lv_obj_t *panel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(panel, UI_COLOR_BONE, 0);
    // Clickable so taps don't reach the counter underneath.
    lv_obj_set_clickable(panel, true);
    lv_obj_set_scrollable(panel, false);
    if (title) {
        lv_obj_t *label = ui_add_label(panel, &lv_font_montserrat_20, title);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, PANEL_TITLE_TOP);
    }
    return panel;
}

void ui_panel_add_underline(lv_obj_t *panel)
{
    lv_obj_t *bar = lv_obj_create(panel);
    lv_obj_remove_style_all(bar);
    lv_obj_set_clickable(bar, false);
    lv_obj_set_size(bar, PANEL_UNDERLINE_W, PANEL_UNDERLINE_H);
    lv_obj_set_style_bg_color(bar, UI_COLOR_VIOLET, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, PANEL_UNDERLINE_Y);
}

void ui_style_button(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, UI_COLOR_VIOLET, 0);
    lv_obj_set_style_text_color(btn, UI_COLOR_VIOLET_INK, 0);
    lv_obj_set_style_radius(btn, UI_BUTTON_RADIUS, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

lv_obj_t *ui_add_done_button(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    ui_style_button(btn);
    lv_obj_set_size(btn, PANEL_DONE_WIDTH, PANEL_BUTTON_HEIGHT);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_add_label(btn, &lv_font_montserrat_20, "Done"));
    return btn;
}

bool ui_back(void)
{
    // Topmost first: a dial or the picker, then the menu ring (the Custom ring
    // sits over the swatch ring), then the full-screen panels.
    return ui_dial_close() || ui_colors_picker_close() || ui_menu_back() || ui_colors_close()
        || ui_history_close();
}

static void ui_show_splash(void)
{
    // Only a real power-on gets the splash; after a panic, watchdog or software
    // reset the counter should be back instantly.
    if (esp_reset_reason() != ESP_RST_POWERON) {
        return;
    }
    lv_obj_t *splash = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(splash);
    lv_obj_set_size(splash, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(splash, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(splash, LV_OPA_COVER, 0);
    // Clickable so taps don't reach the counter underneath.
    lv_obj_set_clickable(splash, true);
    lv_obj_set_scrollable(splash, false);
    lv_obj_t *logo = logo_create(splash, SPLASH_LOGO_SIZE);
    lv_obj_center(logo);
    lv_obj_fade_out(splash, SPLASH_FADE_MS, SPLASH_HOLD_MS);
    lv_obj_delete_delayed(splash, SPLASH_HOLD_MS + SPLASH_FADE_MS);
}

void ui_start(lv_indev_t *touch)
{
    lvgl_port_lock(0);
    lv_indev_set_long_press_time(touch, LONG_PRESS_MS);
    lv_indev_set_long_press_repeat_time(touch, LONG_PRESS_REPEAT_MS);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_text_color(scr, lv_color_white(), 0);
    ui_counter_start(scr);
    ui_show_splash();

    s_tick_timer = lv_timer_create(tick, s_tick_period_ms, NULL);
    lvgl_port_unlock();
}

void ui_refresh_counter(void)
{
    ui_counter_refresh(false, lv_tick_get());
}

void ui_show_message(const char *text)
{
    if (!lvgl_port_lock(100)) {
        return;
    }
    ui_menu_close();
    ui_counter_stop();
    lv_obj_t *scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_t *label = ui_add_label(scr, &lv_font_montserrat_28, text);
    lv_obj_set_width(label, 200);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lvgl_port_unlock();
}
