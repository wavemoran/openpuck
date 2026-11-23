#include "counter.h"

#include "esp_log.h"
#include "puck_save_policy.h"
#include "puck_state_codec.h"
#include "storage.h"

static const char *TAG = "counter";

#define KEY_GAME               "game"
#define KEY_HISTORY            "history"
#define KEY_TIMER              "timer"
#define KEY_SETTINGS           "settings"
#define DEFAULT_PLAYERS        1
#define STARTING_VALUE         0
#define HISTORY_COMBINE_MS     1500
#define SAVE_IDLE_MS           2000
#define SAVE_MAX_DELAY_MS      15000
#define TIMER_RESAVE_MS        30000  // while running, bounds the loss on power cut
#define BLOB_MAX               PUCK_HISTORY_ENCODED_MAX

typedef bool (*save_fn_t)(const char *key, const uint8_t *buf, size_t len);

static puck_game_t s_game;
static puck_history_t s_history;
static puck_timer_t s_timer;
static puck_settings_t s_settings;
static puck_save_policy_t s_policy;
static counter_settings_cb_t s_settings_cb;
static bool s_dirty_game;
static bool s_dirty_history;
static bool s_dirty_timer;
static uint32_t s_timer_saved_ms;
static volatile bool s_timer_running;

static uint32_t timer_duration_ms(void)
{
    return (uint32_t)s_settings.timer_minutes * 60000u;
}

static void sync_timer_flag(void)
{
    s_timer_running = puck_timer_running(&s_timer);
}

static void load_all(void)
{
    uint8_t buf[BLOB_MAX];
    size_t len = sizeof(buf);

    puck_settings_defaults(&s_settings);
    if (storage_load(KEY_SETTINGS, buf, &len)) {
        puck_settings_t loaded;
        if (puck_settings_decode(buf, len, &loaded) == PUCK_STATE_OK) {
            s_settings = loaded;
        } else {
            ESP_LOGW(TAG, "saved settings rejected; using defaults");
        }
    }

    puck_game_new(&s_game, DEFAULT_PLAYERS, STARTING_VALUE);
    len = sizeof(buf);
    if (storage_load(KEY_GAME, buf, &len)) {
        puck_state_result_t res = puck_state_decode(buf, len, &s_game);
        if (res == PUCK_STATE_OK) {
            ESP_LOGI(TAG, "restored game: %d player(s), life %d", s_game.player_count, s_game.life[0]);
        } else {
            ESP_LOGW(TAG, "saved game rejected (code %d); starting fresh", res);
        }
    }

    puck_history_init(&s_history, HISTORY_COMBINE_MS);
    len = sizeof(buf);
    if (storage_load(KEY_HISTORY, buf, &len)) {
        puck_state_result_t res = puck_history_decode(buf, len, &s_history, HISTORY_COMBINE_MS);
        if (res != PUCK_STATE_OK) {
            ESP_LOGW(TAG, "saved history rejected (code %d); starting empty", res);
        }
    }

    puck_timer_init(&s_timer, timer_duration_ms());
    len = sizeof(buf);
    if (storage_load(KEY_TIMER, buf, &len)) {
        puck_state_result_t res = puck_timer_decode(buf, len, &s_timer);
        if (res != PUCK_STATE_OK) {
            ESP_LOGW(TAG, "saved timer rejected (code %d); resetting", res);
            puck_timer_init(&s_timer, timer_duration_ms());
        }
    }
}

void counter_init(void)
{
    puck_save_policy_init(&s_policy, SAVE_IDLE_MS, SAVE_MAX_DELAY_MS);
    load_all();
    sync_timer_flag();
}

const puck_game_t *counter_game(void)
{
    return &s_game;
}

static void note_change(bool game, bool history, bool timer, uint32_t now_ms)
{
    s_dirty_game |= game;
    s_dirty_history |= history;
    s_dirty_timer |= timer;
    puck_save_policy_note_change(&s_policy, now_ms);
}

int16_t counter_adjust(uint8_t player, int16_t delta, uint32_t now_ms)
{
    const int16_t old_total = s_game.life[player];
    if (puck_game_adjust(&s_game, player, delta)) {
        const int16_t new_total = s_game.life[player];
        puck_history_record(&s_history, player, (int16_t)(new_total - old_total), new_total, now_ms);
        note_change(true, true, false, now_ms);
    }
    return s_game.life[player];
}

void counter_new_game(uint8_t player_count, uint32_t now_ms)
{
    if (puck_game_new(&s_game, player_count, STARTING_VALUE)) {
        puck_history_clear(&s_history);
        // The timer is left alone: a best-of-three round runs on one clock.
        note_change(true, true, false, now_ms);
    }
}

const puck_history_t *counter_history(void)
{
    return &s_history;
}

const puck_timer_t *counter_timer(void)
{
    return &s_timer;
}

void counter_timer_toggle(uint32_t now_ms)
{
    if (puck_timer_running(&s_timer)) {
        puck_timer_pause(&s_timer, now_ms);
    } else {
        puck_timer_start(&s_timer, now_ms);
        s_timer_saved_ms = now_ms;
    }
    sync_timer_flag();
    note_change(false, false, true, now_ms);
}

void counter_timer_reset(uint32_t now_ms)
{
    puck_timer_reset(&s_timer);
    sync_timer_flag();
    note_change(false, false, true, now_ms);
}

bool counter_timer_running(void)
{
    return s_timer_running;
}

const puck_settings_t *counter_settings(void)
{
    return &s_settings;
}

bool counter_set_settings(const puck_settings_t *settings, uint32_t now_ms)
{
    if (!puck_settings_is_valid(settings)) {
        return false;
    }
    const bool duration_changed = settings->timer_minutes != s_settings.timer_minutes;
    s_settings = *settings;
    if (duration_changed) {
        puck_timer_set_duration(&s_timer, timer_duration_ms());
        note_change(false, false, true, now_ms);
    }
    uint8_t buf[PUCK_SETTINGS_ENCODED_MAX];
    size_t len = puck_settings_encode(&s_settings, buf, sizeof(buf));
    if (len) {
        storage_save_async(KEY_SETTINGS, buf, len);
    }
    if (s_settings_cb) {
        s_settings_cb(&s_settings);
    }
    return true;
}

void counter_set_timer_enabled(bool enabled, uint32_t now_ms)
{
    puck_settings_t settings = s_settings;
    settings.timer_enabled = enabled;
    counter_set_settings(&settings, now_ms);
    if (!enabled) {
        counter_timer_reset(now_ms);
    }
}

void counter_set_settings_callback(counter_settings_cb_t cb)
{
    s_settings_cb = cb;
}

static bool save_timer(save_fn_t save, uint32_t now_ms)
{
    uint8_t buf[PUCK_TIMER_STATE_SIZE];
    size_t len = puck_timer_encode(&s_timer, now_ms, buf, sizeof(buf));
    if (!len || !save(KEY_TIMER, buf, len)) {
        return false;
    }
    s_dirty_timer = false;
    s_timer_saved_ms = now_ms;
    return true;
}

// Save the blobs with unsaved changes; true if all of them were saved.
static bool save_dirty(save_fn_t save, uint32_t now_ms)
{
    uint8_t buf[BLOB_MAX];
    bool ok = true;

    if (s_dirty_game) {
        size_t len = puck_state_encode(&s_game, buf, sizeof(buf));
        if (len && save(KEY_GAME, buf, len)) {
            s_dirty_game = false;
        } else {
            ok = false;
        }
    }
    if (s_dirty_history) {
        size_t len = puck_history_encode(&s_history, buf, sizeof(buf));
        if (len && save(KEY_HISTORY, buf, len)) {
            s_dirty_history = false;
        } else {
            ok = false;
        }
    }
    if (s_dirty_timer && !save_timer(save, now_ms)) {
        ok = false;
    }
    return ok;
}

static bool save_queued(const char *key, const uint8_t *buf, size_t len)
{
    storage_save_async(key, buf, len);
    return true;
}

void counter_poll(uint32_t now_ms)
{
    if (puck_save_policy_due(&s_policy, now_ms)) {
        save_dirty(save_queued, now_ms);
        puck_save_policy_mark_saved(&s_policy);
    }
    if (s_timer_running && now_ms - s_timer_saved_ms >= TIMER_RESAVE_MS) {
        save_timer(save_queued, now_ms);
    }
}

bool counter_save_pending(void)
{
    return puck_save_policy_pending(&s_policy);
}

void counter_flush(uint32_t now_ms)
{
    bool ok = save_dirty(storage_save, now_ms);
    if (s_timer_running && !s_dirty_timer) {
        ok = save_timer(storage_save, now_ms) && ok;
    }
    if (ok) {
        puck_save_policy_mark_saved(&s_policy);
    }
}
