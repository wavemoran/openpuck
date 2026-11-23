// Owns the game, its history, the timer and the settings, and saves them in
// the background when the save policy says so. Call everything except
// counter_init and counter_timer_running from the LVGL task, or while holding
// the LVGL lock.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "puck_game.h"
#include "puck_history.h"
#include "puck_settings.h"
#include "puck_timer.h"

// Load settings, game, history and timer (defaults for anything missing or
// corrupt). Call after storage_init.
void counter_init(void);

const puck_game_t *counter_game(void);

// Change a player's life; returns the new total. The applied change (after
// clamping) is recorded in the history.
int16_t counter_adjust(uint8_t player, int16_t delta, uint32_t now_ms);

// Start a new game at 0: clears the history. The timer is left as it is (a
// round of several games runs on one clock); reset it from the Timer menu.
void counter_new_game(uint8_t player_count, uint32_t now_ms);

const puck_history_t *counter_history(void);

const puck_timer_t *counter_timer(void);
// Start (from idle or paused) or pause.
void counter_timer_toggle(uint32_t now_ms);
void counter_timer_reset(uint32_t now_ms);
// Turn the timer on or off (saved with the settings). Turning it off also
// resets it, so it's idle and no longer inhibits auto-off.
void counter_set_timer_enabled(bool enabled, uint32_t now_ms);
// Safe from ANY task (reads a flag kept in sync with the timer state).
bool counter_timer_running(void);

const puck_settings_t *counter_settings(void);
// Validate, store and save the settings, then call the settings callback.
// Returns false (and changes nothing) if they are invalid.
bool counter_set_settings(const puck_settings_t *settings, uint32_t now_ms);

typedef void (*counter_settings_cb_t)(const puck_settings_t *settings);
void counter_set_settings_callback(counter_settings_cb_t cb);

// Call periodically: queues background saves when due.
void counter_poll(uint32_t now_ms);

// True while a change is waiting for counter_poll to save it.
bool counter_save_pending(void);

// Save everything unsaved now (before power-off).
void counter_flush(uint32_t now_ms);
