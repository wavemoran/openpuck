#include "puck_settings.h"

#include "puck_color.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t MAGIC[4] = { 'O', 'P', 'S', 'T' };
#define SETTINGS_VERSION 6
#define HEADER_SIZE      5   // magic + version
#define CRC_SIZE         4
#define MIN_ENCODED_SIZE (HEADER_SIZE + CRC_SIZE)
#define V2_FIELD_BYTES  4
#define COLOR_BYTES     (PUCK_MAX_PLAYERS * 6)  // per player: background RGB, text RGB
#define TIMER_ENABLED_BYTES 1
#define BRIGHTNESS_BYTES    1
#define P2_SAME_WAY_BYTES   1
#define COLOR_MAX       0xFFFFFFu

static const uint8_t AUTO_OFF_CHOICES[] = { 1, 2, 5, 10, 15, 20, 30, 45, 60 };
#define AUTO_OFF_COUNT (sizeof(AUTO_OFF_CHOICES) / sizeof(AUTO_OFF_CHOICES[0]))

void puck_settings_defaults(puck_settings_t *settings)
{
    settings->auto_off_enabled = true;
    settings->auto_off_minutes = PUCK_AUTO_OFF_DEFAULT_MINUTES;
    settings->timer_minutes = PUCK_TIMER_DEFAULT_MINUTES;
    settings->battery_display = PUCK_BATTERY_DISPLAY_ICON;
    settings->timer_enabled = true;
    settings->brightness = PUCK_BRIGHTNESS_DEFAULT;
    settings->p2_same_way = false;
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        settings->player_background[i] = PUCK_COLOR_BLACK;
        settings->player_text[i] = PUCK_COLOR_WHITE;
    }
}

static bool is_auto_off_choice(uint8_t minutes)
{
    for (size_t i = 0; i < AUTO_OFF_COUNT; i++) {
        if (AUTO_OFF_CHOICES[i] == minutes) {
            return true;
        }
    }
    return false;
}

bool puck_settings_is_valid(const puck_settings_t *settings)
{
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        if (settings->player_background[i] > COLOR_MAX || settings->player_text[i] > COLOR_MAX) {
            return false;
        }
    }
    return is_auto_off_choice(settings->auto_off_minutes)
        && settings->timer_minutes >= PUCK_TIMER_MIN_MINUTES
        && settings->timer_minutes <= PUCK_TIMER_MAX_MINUTES
        && settings->timer_minutes % PUCK_TIMER_STEP_MINUTES == 0
        && settings->battery_display <= PUCK_BATTERY_DISPLAY_ICON_PERCENT
        && settings->brightness >= PUCK_BRIGHTNESS_MIN
        && settings->brightness <= PUCK_BRIGHTNESS_MAX
        && settings->brightness % PUCK_BRIGHTNESS_STEP == 0;
}

uint8_t puck_settings_step_auto_off(uint8_t minutes, int dir)
{
    if (dir > 0) {
        for (size_t i = 0; i < AUTO_OFF_COUNT; i++) {
            if (AUTO_OFF_CHOICES[i] > minutes) {
                return AUTO_OFF_CHOICES[i];
            }
        }
        return AUTO_OFF_CHOICES[AUTO_OFF_COUNT - 1];
    }
    if (dir < 0) {
        for (size_t i = AUTO_OFF_COUNT; i > 0; i--) {
            if (AUTO_OFF_CHOICES[i - 1] < minutes) {
                return AUTO_OFF_CHOICES[i - 1];
            }
        }
        return AUTO_OFF_CHOICES[0];
    }
    uint8_t best = AUTO_OFF_CHOICES[0];
    int best_dist = abs((int)minutes - (int)best);
    for (size_t i = 1; i < AUTO_OFF_COUNT; i++) {
        int dist = abs((int)minutes - (int)AUTO_OFF_CHOICES[i]);
        if (dist < best_dist) {  // strict: ties keep the smaller choice
            best = AUTO_OFF_CHOICES[i];
            best_dist = dist;
        }
    }
    return best;
}

uint8_t puck_settings_auto_off_dial_index(bool enabled, uint8_t minutes)
{
    if (!enabled) {
        return 0;
    }
    const uint8_t choice = puck_settings_step_auto_off(minutes, 0);
    // choice is always one of the choices; the last needs no comparison.
    size_t i = 0;
    while (i + 1 < AUTO_OFF_COUNT && AUTO_OFF_CHOICES[i] != choice) {
        i++;
    }
    return (uint8_t)(i + 1);
}

void puck_settings_step_auto_off_dial(bool enabled, uint8_t minutes, int dir,
                                      bool *enabled_out, uint8_t *minutes_out)
{
    _Static_assert(AUTO_OFF_COUNT + 1 == PUCK_AUTO_OFF_DIAL_STOPS, "Off plus each choice");
    const uint8_t snapped = puck_settings_step_auto_off(minutes, 0);
    *enabled_out = enabled;
    *minutes_out = snapped;
    const uint8_t first = AUTO_OFF_CHOICES[0];
    const uint8_t last = AUTO_OFF_CHOICES[AUTO_OFF_COUNT - 1];
    if (dir > 0) {
        if (!enabled) {
            *enabled_out = true;
            *minutes_out = first;
        } else if (snapped == last) {
            *enabled_out = false;          // wraps from 60 to Off
        } else {
            *minutes_out = puck_settings_step_auto_off(snapped, 1);
        }
    } else if (dir < 0) {
        if (!enabled) {
            *enabled_out = true;           // wraps from Off to 60
            *minutes_out = last;
        } else if (snapped == first) {
            *enabled_out = false;
        } else {
            *minutes_out = puck_settings_step_auto_off(snapped, -1);
        }
    }
}

uint8_t puck_settings_step_timer(uint8_t minutes, int dir)
{
    int next = minutes;
    if (dir > 0) {
        next += PUCK_TIMER_STEP_MINUTES;
    } else if (dir < 0) {
        next -= PUCK_TIMER_STEP_MINUTES;
    }
    if (next < PUCK_TIMER_MIN_MINUTES) {
        next = PUCK_TIMER_MIN_MINUTES;
    }
    if (next > PUCK_TIMER_MAX_MINUTES) {
        next = PUCK_TIMER_MAX_MINUTES;
    }
    return (uint8_t)next;
}

uint8_t puck_settings_step_brightness(uint8_t percent, int dir)
{
    int next = percent;
    if (dir > 0) {
        next += PUCK_BRIGHTNESS_STEP;
    } else if (dir < 0) {
        next -= PUCK_BRIGHTNESS_STEP;
    }
    if (next < PUCK_BRIGHTNESS_MIN) {
        next = PUCK_BRIGHTNESS_MIN;
    }
    if (next > PUCK_BRIGHTNESS_MAX) {
        next = PUCK_BRIGHTNESS_MAX;
    }
    return (uint8_t)next;
}

// Total encoded size for a version, or 0 if the version is unknown. A new
// version appends field bytes before the CRC and adds a case here.
static size_t encoded_size(uint8_t version)
{
    switch (version) {
    case 1:
    case 2:  // same layout; byte 8 widened from a bool to battery_display
        return HEADER_SIZE + V2_FIELD_BYTES + CRC_SIZE;
    case 3:
        return HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + CRC_SIZE;
    case 4:
        return HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + TIMER_ENABLED_BYTES + CRC_SIZE;
    case 5:
        return HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + TIMER_ENABLED_BYTES + BRIGHTNESS_BYTES
               + CRC_SIZE;
    case 6:
        return HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + TIMER_ENABLED_BYTES + BRIGHTNESS_BYTES
               + P2_SAME_WAY_BYTES + CRC_SIZE;
    default:
        return 0;
    }
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static uint32_t get_u32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)p[i] << (8 * i);
    }
    return v;
}

size_t puck_settings_encode(const puck_settings_t *settings, uint8_t *buf, size_t cap)
{
    size_t size = encoded_size(SETTINGS_VERSION);
    if (cap < size) {
        return 0;
    }
    memcpy(buf, MAGIC, sizeof(MAGIC));
    buf[4] = SETTINGS_VERSION;
    buf[5] = settings->auto_off_enabled ? 1 : 0;
    buf[6] = settings->auto_off_minutes;
    buf[7] = settings->timer_minutes;
    buf[8] = settings->battery_display;
    uint8_t *p = buf + HEADER_SIZE + V2_FIELD_BYTES;
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        uint32_t colors[2] = { settings->player_background[i], settings->player_text[i] };
        for (int c = 0; c < 2; c++) {
            *p++ = (uint8_t)(colors[c] >> 16);
            *p++ = (uint8_t)(colors[c] >> 8);
            *p++ = (uint8_t)colors[c];
        }
    }
    *p++ = settings->timer_enabled ? 1 : 0;
    *p++ = settings->brightness;
    *p = settings->p2_same_way ? 1 : 0;
    put_u32(buf + size - CRC_SIZE, puck_crc32(buf, size - CRC_SIZE));
    return size;
}

puck_state_result_t puck_settings_decode(const uint8_t *buf, size_t len, puck_settings_t *settings)
{
    if (len < MIN_ENCODED_SIZE) {
        return PUCK_STATE_ERR_SIZE;
    }
    if (memcmp(buf, MAGIC, sizeof(MAGIC)) != 0) {
        return PUCK_STATE_ERR_MAGIC;
    }
    uint8_t version = buf[4];
    size_t size = encoded_size(version);
    if (size == 0) {
        return PUCK_STATE_ERR_VERSION;
    }
    if (len != size) {
        return PUCK_STATE_ERR_SIZE;
    }
    if (get_u32(buf + size - CRC_SIZE) != puck_crc32(buf, size - CRC_SIZE)) {
        return PUCK_STATE_ERR_CRC;
    }

    // Fields added by later versions keep their defaults when decoding older data.
    puck_settings_t decoded;
    puck_settings_defaults(&decoded);
    if (version >= 1) {
        if (buf[5] > 1) {
            return PUCK_STATE_ERR_INVALID;
        }
        decoded.auto_off_enabled = buf[5] == 1;
        decoded.auto_off_minutes = buf[6];
        decoded.timer_minutes = buf[7];
        if (version == 1) {
            // Version 1 stored a battery-icon on/off bool here.
            if (buf[8] > 1) {
                return PUCK_STATE_ERR_INVALID;
            }
            decoded.battery_display = buf[8] ? PUCK_BATTERY_DISPLAY_ICON : PUCK_BATTERY_DISPLAY_OFF;
        } else {
            decoded.battery_display = buf[8];
        }
    }
    if (version >= 3) {
        const uint8_t *p = buf + HEADER_SIZE + V2_FIELD_BYTES;
        for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
            decoded.player_background[i] = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
            decoded.player_text[i] = ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 8) | p[5];
            p += 6;
        }
    }
    if (version >= 4) {
        uint8_t enabled = buf[HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES];
        if (enabled > 1) {
            return PUCK_STATE_ERR_INVALID;
        }
        decoded.timer_enabled = enabled == 1;
    }
    if (version >= 5) {
        decoded.brightness = buf[HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + TIMER_ENABLED_BYTES];
    }
    if (version >= 6) {
        uint8_t same_way = buf[HEADER_SIZE + V2_FIELD_BYTES + COLOR_BYTES + TIMER_ENABLED_BYTES
                               + BRIGHTNESS_BYTES];
        if (same_way > 1) {
            return PUCK_STATE_ERR_INVALID;
        }
        decoded.p2_same_way = same_way == 1;
    }
    if (!puck_settings_is_valid(&decoded)) {
        return PUCK_STATE_ERR_INVALID;
    }
    *settings = decoded;
    return PUCK_STATE_OK;
}
