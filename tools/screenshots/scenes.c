// Scripted scenes for the documentation screenshots. Each scene starts from a
// clean slate (own process), drives the real UI with simulated touches and
// saves a PNG. Run: screenshots [--out DIR] [--no-bezel] [scene ...]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "counter.h"
#include "host_hw.h"
#include "puck_color.h"
#include "render.h"
#include "ui.h"

#define CENTER 120

// Fixed readings so every run is identical.
#define BATTERY_PERCENT 76
#define BATTERY_MV      3950

static int s_failed;

static void check(bool ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "scene check failed: %s\n", what);
        s_failed = 1;
    }
}

// ---- Helpers ----

static void start_ui(bool power_on)
{
    render_begin();
    host_set_reset_reason(power_on ? ESP_RST_POWERON : ESP_RST_SW);
    host_battery_set(BATTERY_PERCENT, BATTERY_MV, false);
    counter_init();
    ui_start(render_indev());
    render_advance(100);
}

static void update_settings(void (*change)(puck_settings_t *))
{
    puck_settings_t settings = *counter_settings();
    change(&settings);
    check(counter_set_settings(&settings, render_now()), "valid settings");
    ui_refresh_counter();
    render_advance(100);
}

static void battery_percent(puck_settings_t *s)
{
    s->battery_display = PUCK_BATTERY_DISPLAY_ICON_PERCENT;
}

// The timer paused one second into its countdown (shows 54:59).
static void timer_paused_at_start(void)
{
    counter_timer_toggle(render_now());
    ui_refresh_counter();
    render_advance(1000);
    counter_timer_toggle(render_now());
    ui_refresh_counter();
    render_advance(100);
}

static void one_player(int total)
{
    update_settings(battery_percent);
    counter_adjust(0, (int16_t)total, render_now());
    timer_paused_at_start();
    check(counter_game()->life[0] == total, "1 player total");
}

// Find a label by its text anywhere on the screen or the top layer and tap it.
static lv_obj_t *find_label(lv_obj_t *obj, const char *text)
{
    if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) {
        return obj;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) {
        lv_obj_t *found = find_label(lv_obj_get_child(obj, (int32_t)i), text);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static void tap_label(const char *text, uint32_t settle_ms)
{
    lv_obj_t *label = find_label(lv_layer_top(), text);
    if (!label) {
        label = find_label(lv_screen_active(), text);
    }
    check(label != NULL, text);
    if (!label) {
        return;
    }
    lv_obj_update_layout(label);
    lv_area_t area;
    lv_obj_get_coords(label, &area);
    touch_tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2, settle_ms);
}

static void open_menu(void)
{
    touch_hold(CENTER, CENTER, 600, 300);
}

// Quadrant centers for the menu.
#define Q_TOP_LEFT     60, 60
#define Q_TOP_RIGHT    180, 60
#define Q_BOTTOM_LEFT  60, 180
#define Q_BOTTOM_RIGHT 180, 180
// Halves of the two-zone Custom ring.
#define H_TOP          CENTER, 40
#define H_BOTTOM       CENTER, 200
// The Custom swatch on the Colors ring (at 60 degrees, radius 94).
#define SWATCH_CUSTOM  167, 201

// ---- Scenes ----

static void scene_counter_1p(void)
{
    start_ui(false);
    one_player(20);
    render_save("counter-1p");
}

static void scene_counter_1p_change(void)
{
    start_ui(false);
    one_player(18);
    touch_tap(CENTER, 40, 150);
    touch_tap(CENTER, 40, 30);
    check(counter_game()->life[0] == 20, "two taps from 18");
    render_save("counter-1p-change");
}

static void scene_counter_1p_six(void)
{
    start_ui(false);
    one_player(6);
    render_save("counter-1p-six");
}

// The timer run 42 s past its 55 minutes: the overtime look.
static void scene_counter_1p_overtime(void)
{
    start_ui(false);
    update_settings(battery_percent);
    counter_adjust(0, 13, render_now());
    counter_timer_toggle(render_now());
    ui_refresh_counter();
    render_advance(100);
    render_jump(55 * 60 * 1000 + 42 * 1000);
    render_advance(300);
    check(counter_timer_running(), "timer running");
    check(counter_game()->life[0] == 13, "1 player total");
    render_save("counter-1p-overtime");
}

static void use_preset(int index, uint8_t player)
{
    puck_settings_t settings = *counter_settings();
    settings.player_background[player] = PUCK_COLOR_PRESETS[index].background;
    settings.player_text[player] = PUCK_COLOR_PRESETS[index].text;
    check(counter_set_settings(&settings, render_now()), "preset");
}

static void tap_player(uint8_t player, bool plus, int count)
{
    // Player 0 is the bottom half; player 1 the top half, turned around, so
    // their plus is on the screen's left.
    const int32_t y = player == 0 ? 180 : 60;
    const int32_t x = (plus == (player == 0)) ? 180 : 60;
    for (int i = 0; i < count; i++) {
        touch_tap(x, y, 200);
    }
    render_advance(2000);   // the next change is a new history entry
}

static void hold_player(uint8_t player, bool plus, uint32_t ms)
{
    const int32_t y = player == 0 ? 180 : 60;
    const int32_t x = (plus == (player == 0)) ? 180 : 60;
    touch_hold(x, y, ms, 2000);
}

// Two players: P1 20 -> 14 and P2 20 -> 17 through taps and holds, in Blue
// and Red or (colored false) on the default black background.
static void two_players_in(bool colored)
{
    start_ui(false);
    counter_new_game(2, render_now());
    if (colored) {
        use_preset(1, 0);   // Blue
        use_preset(3, 1);   // Red
    }
    ui_refresh_counter();
    render_advance(100);
    counter_adjust(0, 20, render_now());
    counter_adjust(1, 20, render_now());
    ui_refresh_counter();
    render_advance(2000);

    tap_player(0, false, 3);        // P1 -3
    tap_player(1, false, 1);        // P2 -1
    hold_player(0, false, 700);     // P1 -5
    hold_player(1, false, 700);     // P2 -5
    tap_player(0, true, 2);         // P1 +2
    tap_player(1, true, 3);         // P2 +3
    check(counter_game()->life[0] == 14, "P1 total");
    check(counter_game()->life[1] == 17, "P2 total");
    check(puck_history_count(counter_history()) == 8, "history entries");
}

static void two_players(void)
{
    two_players_in(true);
}

static void scene_counter_2p(void)
{
    two_players();
    update_settings(battery_percent);
    counter_timer_toggle(render_now());
    ui_refresh_counter();
    render_advance(100);
    render_jump(13 * 60 * 1000 + 53 * 1000);   // 41:07 left
    render_advance(300);
    check(counter_timer_running(), "timer running");
    render_save("counter-2p");
}

static void scene_menu(void)
{
    start_ui(false);
    one_player(20);
    open_menu();
    render_save("menu");
}

static void scene_menu_new_game(void)
{
    two_players_in(false);
    open_menu();
    touch_tap(Q_TOP_LEFT, 300);
    render_save("menu-new-game");
}

static void scene_menu_timer(void)
{
    start_ui(false);
    one_player(20);
    open_menu();
    touch_tap(Q_TOP_RIGHT, 300);
    render_save("menu-timer");
}

static void timer_off(puck_settings_t *s)
{
    s->timer_enabled = false;
}

static void scene_menu_timer_off(void)
{
    start_ui(false);
    one_player(20);
    update_settings(timer_off);
    open_menu();
    touch_tap(Q_TOP_RIGHT, 300);
    render_save("menu-timer-off");
}

static void scene_timer_set(void)
{
    start_ui(false);
    one_player(20);
    open_menu();
    touch_tap(Q_TOP_RIGHT, 300);
    touch_tap(Q_BOTTOM_RIGHT, 300);
    render_save("timer-set");
}

static void scene_history(void)
{
    two_players();
    open_menu();
    touch_tap(Q_BOTTOM_RIGHT, 300);
    render_save("history");
}

static void scene_history_default_colors(void)
{
    two_players_in(false);
    open_menu();
    touch_tap(Q_BOTTOM_RIGHT, 300);
    render_save("history-default-colors");
}

static void open_settings(void)
{
    open_menu();
    touch_tap(Q_BOTTOM_LEFT, 300);
}

static void scene_settings(void)
{
    start_ui(false);
    one_player(20);
    open_settings();
    check(find_label(lv_layer_top(), "v0.0.2") != NULL, "version under Back");
    render_save("settings");
}

static void scene_brightness(void)
{
    start_ui(false);
    one_player(20);
    open_settings();
    touch_tap(Q_TOP_RIGHT, 300);
    render_save("brightness");
}

static void scene_auto_off(void)
{
    start_ui(false);
    one_player(20);
    open_settings();
    touch_tap(Q_BOTTOM_LEFT, 300);
    render_save("auto-off");
}

static void auto_off_disabled(puck_settings_t *s)
{
    s->auto_off_enabled = false;
}

static void scene_auto_off_off(void)
{
    start_ui(false);
    one_player(20);
    update_settings(auto_off_disabled);
    open_settings();
    touch_tap(Q_BOTTOM_LEFT, 300);
    render_save("auto-off-off");
}

static void scene_colors(void)
{
    start_ui(false);
    one_player(20);
    open_settings();
    touch_tap(Q_TOP_LEFT, 300);
    render_save("colors");
}

static void scene_colors_2p(void)
{
    two_players();
    open_settings();
    touch_tap(Q_TOP_LEFT, 300);
    render_save("colors-2p");
}

static void scene_colors_custom(void)
{
    start_ui(false);
    one_player(20);
    use_preset(1, 0);   // Blue
    ui_refresh_counter();
    render_advance(100);
    open_settings();
    touch_tap(Q_TOP_LEFT, 300);
    touch_tap(SWATCH_CUSTOM, 300);
    render_save("colors-custom");
}

static void scene_color_picker(void)
{
    start_ui(false);
    one_player(20);
    use_preset(1, 0);   // Blue
    ui_refresh_counter();
    render_advance(100);
    open_settings();
    touch_tap(Q_TOP_LEFT, 300);
    touch_tap(SWATCH_CUSTOM, 300);
    touch_tap(H_TOP, 300);
    touch_tap(139, 14, 300);   // pick a hue on the ring (top, about 280 degrees)
    render_save("color-picker");
}

static void same_way(puck_settings_t *s)
{
    s->p2_same_way = true;
}

static void scene_counter_2p_same_way(void)
{
    two_players_in(false);
    update_settings(same_way);
    timer_paused_at_start();
    render_advance(2000);   // let the running changes fade
    render_save("counter-2p-same-way");
}

static void scene_splash(void)
{
    start_ui(true);
    render_save("splash");
}

static const struct {
    const char *name;
    void (*run)(void);
} SCENES[] = {
    { "counter-1p", scene_counter_1p },
    { "counter-1p-change", scene_counter_1p_change },
    { "counter-1p-six", scene_counter_1p_six },
    { "counter-1p-overtime", scene_counter_1p_overtime },
    { "counter-2p", scene_counter_2p },
    { "counter-2p-same-way", scene_counter_2p_same_way },
    { "menu", scene_menu },
    { "menu-new-game", scene_menu_new_game },
    { "menu-timer", scene_menu_timer },
    { "menu-timer-off", scene_menu_timer_off },
    { "timer-set", scene_timer_set },
    { "history", scene_history },
    { "history-default-colors", scene_history_default_colors },
    { "settings", scene_settings },
    { "brightness", scene_brightness },
    { "auto-off", scene_auto_off },
    { "auto-off-off", scene_auto_off_off },
    { "colors", scene_colors },
    { "colors-2p", scene_colors_2p },
    { "colors-custom", scene_colors_custom },
    { "color-picker", scene_color_picker },
    { "splash", scene_splash },
};
#define SCENE_COUNT (sizeof(SCENES) / sizeof(SCENES[0]))

// Each scene runs in its own process: the UI keeps its state in statics, so a
// fresh process is the simplest way to start every scene from scratch.
static int run_scene(size_t index)
{
    fflush(stdout);
    const pid_t pid = fork();
    if (pid == 0) {
        SCENES[index].run();
        fflush(stdout);
        _exit(s_failed);
    }
    int status = 0;
    if (pid < 0 || waitpid(pid, &status, 0) < 0) {
        return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int main(int argc, char **argv)
{
    const char *dir = SCREENSHOT_OUT_DEFAULT;
    bool bezel = true;
    int first = 1;
    for (; first < argc; first++) {
        if (strcmp(argv[first], "--out") == 0 && first + 1 < argc) {
            dir = argv[++first];
        } else if (strcmp(argv[first], "--no-bezel") == 0) {
            bezel = false;
        } else {
            break;
        }
    }
    render_set_output(dir, bezel);

    int failed = 0;
    if (first == argc) {
        for (size_t i = 0; i < SCENE_COUNT; i++) {
            failed |= run_scene(i);
        }
        return failed;
    }
    for (int a = first; a < argc; a++) {
        size_t i = 0;
        while (i < SCENE_COUNT && strcmp(SCENES[i].name, argv[a]) != 0) {
            i++;
        }
        if (i == SCENE_COUNT) {
            fprintf(stderr, "unknown scene: %s\n", argv[a]);
            return 2;
        }
        failed |= run_scene(i);
    }
    return failed;
}
