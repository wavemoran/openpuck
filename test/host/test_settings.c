#include <string.h>

#include "puck_settings.h"
#include "unity.h"

// Fixtures for released formats. CRCs were computed independently with
// Python's zlib.crc32. Released fixtures must NEVER be edited: they prove old
// saved data keeps decoding. Add new fixtures for new versions instead.

// Version 1 encoding of the default settings.
static const uint8_t V1_FIXTURE[13] = {
    0x4f, 0x50, 0x53, 0x54, 0x01, 0x01, 0x0a, 0x37,
    0x01, 0xd4, 0xc9, 0x6d, 0x58,
};

// Version 2 encodings, CRC from Python's zlib.crc32:
// the defaults (battery ICON) and the same with battery ICON_PERCENT.
static const uint8_t V2_DEFAULTS_FIXTURE[13] = {
    0x4f, 0x50, 0x53, 0x54, 0x02, 0x01, 0x0a, 0x37,
    0x01, 0x04, 0xb3, 0xcd, 0x1f,
};
static const uint8_t V2_PERCENT_FIXTURE[13] = {
    0x4f, 0x50, 0x53, 0x54, 0x02, 0x01, 0x0a, 0x37,
    0x02, 0xbe, 0xe2, 0xc4, 0x86,
};

// Version 3: defaults except player 0 = Blue preset
// 0x3D91BC with white text, player 1 = Yellow preset 0xD6AC48 with black
// text, battery ICON_PERCENT.
static const uint8_t V3_COLORS_FIXTURE[25] = {
    0x4f, 0x50, 0x53, 0x54, 0x03, 0x01, 0x0a, 0x37,
    0x02, 0x3d, 0x91, 0xbc, 0xff, 0xff, 0xff, 0xd6,
    0xac, 0x48, 0x00, 0x00, 0x00, 0x8d, 0x85, 0x19,
    0xae,
};

// Version 4: the V3 fixture's fields with timer_enabled = 0.
static const uint8_t V4_TIMER_OFF_FIXTURE[26] = {
    0x4f, 0x50, 0x53, 0x54, 0x04, 0x01, 0x0a, 0x37,
    0x02, 0x3d, 0x91, 0xbc, 0xff, 0xff, 0xff, 0xd6,
    0xac, 0x48, 0x00, 0x00, 0x00, 0x00, 0xf1, 0x03,
    0x8b, 0xa0,
};

// Version 5: the V4 fixture's fields with brightness = 30.
static const uint8_t V5_BRIGHTNESS_FIXTURE[27] = {
    0x4f, 0x50, 0x53, 0x54, 0x05, 0x01, 0x0a, 0x37,
    0x02, 0x3d, 0x91, 0xbc, 0xff, 0xff, 0xff, 0xd6,
    0xac, 0x48, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x3f,
    0x1b, 0xf5, 0x35,
};

// Version 6 (current format): the V5 fixture's fields with p2_same_way = 1.
static const uint8_t V6_SAME_WAY_FIXTURE[28] = {
    0x4f, 0x50, 0x53, 0x54, 0x06, 0x01, 0x0a, 0x37,
    0x02, 0x3d, 0x91, 0xbc, 0xff, 0xff, 0xff, 0xd6,
    0xac, 0x48, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x01,
    0xf7, 0x4a, 0xff, 0xbc,
};

static puck_settings_t settings;
static uint8_t buf[PUCK_SETTINGS_ENCODED_MAX];
static size_t encoded_len;

void setUp(void)
{
    puck_settings_defaults(&settings);
    encoded_len = puck_settings_encode(&settings, buf, sizeof(buf));
    TEST_ASSERT_EQUAL(28, encoded_len);
}

void tearDown(void) {}

// Rewrites the CRC so corruption of other fields reaches the content checks.
static void refresh_crc(uint8_t *b, size_t len)
{
    uint32_t crc = puck_crc32(b, len - 4);
    for (int i = 0; i < 4; i++) {
        b[len - 4 + i] = (uint8_t)(crc >> (8 * i));
    }
}

static void expect_rejected(const uint8_t *b, size_t len, puck_state_result_t want)
{
    puck_settings_t out = { .auto_off_enabled = false, .auto_off_minutes = 7,
                            .timer_minutes = 3, .battery_display = 9 };
    TEST_ASSERT_EQUAL(want, puck_settings_decode(b, len, &out));
    TEST_ASSERT_FALSE(out.auto_off_enabled);
    TEST_ASSERT_EQUAL_UINT8(7, out.auto_off_minutes);
    TEST_ASSERT_EQUAL_UINT8(3, out.timer_minutes);
    TEST_ASSERT_EQUAL_UINT8(9, out.battery_display);
}

static void test_defaults(void)
{
    puck_settings_t s;
    memset(&s, 0xff, sizeof(s));
    puck_settings_defaults(&s);
    TEST_ASSERT_TRUE(s.auto_off_enabled);
    TEST_ASSERT_TRUE(s.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(10, s.auto_off_minutes);
    TEST_ASSERT_EQUAL_UINT8(55, s.timer_minutes);
    TEST_ASSERT_EQUAL_UINT8(60, s.brightness);
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON, s.battery_display);
    TEST_ASSERT_FALSE(s.p2_same_way);
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        TEST_ASSERT_EQUAL_HEX32(0x000000, s.player_background[i]);
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, s.player_text[i]);
    }
    TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
}

static void test_is_valid_rejects_out_of_range_colors(void)
{
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        puck_settings_t s = settings;
        s.player_background[i] = 0x1000000u;
        TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
        s = settings;
        s.player_text[i] = 0xFF000000u;
        TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
        s = settings;
        s.player_background[i] = 0xFFFFFFu;
        s.player_text[i] = 0x000000u;
        TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
    }
}

static void test_is_valid_rejects_bad_auto_off_minutes(void)
{
    uint8_t bad[] = { 0, 3, 4, 11, 61, 255 };
    for (size_t i = 0; i < sizeof(bad); i++) {
        puck_settings_t s = settings;
        s.auto_off_minutes = bad[i];
        TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    }
}

static void test_is_valid_accepts_every_auto_off_choice(void)
{
    uint8_t choices[] = { 1, 2, 5, 10, 15, 20, 30, 45, 60 };
    for (size_t i = 0; i < sizeof(choices); i++) {
        puck_settings_t s = settings;
        s.auto_off_minutes = choices[i];
        TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
    }
}

static void test_is_valid_timer_range_and_step(void)
{
    puck_settings_t s = settings;
    s.timer_minutes = PUCK_TIMER_MIN_MINUTES;
    TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
    s.timer_minutes = PUCK_TIMER_MAX_MINUTES;
    TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
    s.timer_minutes = 0;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    s.timer_minutes = PUCK_TIMER_MIN_MINUTES - 1;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    s.timer_minutes = PUCK_TIMER_MAX_MINUTES + 5;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    s.timer_minutes = 255;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    s.timer_minutes = 52;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
}

static void test_step_auto_off_up(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, puck_settings_step_auto_off(1, 1));
    TEST_ASSERT_EQUAL_UINT8(5, puck_settings_step_auto_off(2, 1));
    TEST_ASSERT_EQUAL_UINT8(15, puck_settings_step_auto_off(10, 1));
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_auto_off(45, 1));
}

static void test_step_auto_off_down(void)
{
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_step_auto_off(2, -1));
    TEST_ASSERT_EQUAL_UINT8(5, puck_settings_step_auto_off(10, -1));
    TEST_ASSERT_EQUAL_UINT8(45, puck_settings_step_auto_off(60, -1));
}

static void test_step_auto_off_clamps_at_ends(void)
{
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_auto_off(60, 1));
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_step_auto_off(1, -1));
}

static void test_step_auto_off_from_non_choice(void)
{
    TEST_ASSERT_EQUAL_UINT8(10, puck_settings_step_auto_off(7, 1));
    TEST_ASSERT_EQUAL_UINT8(5, puck_settings_step_auto_off(7, -1));
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_step_auto_off(0, 1));
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_step_auto_off(0, -1));
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_auto_off(200, 1));
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_auto_off(200, -1));
}

static void test_step_auto_off_dir_zero_snaps_to_nearest(void)
{
    TEST_ASSERT_EQUAL_UINT8(10, puck_settings_step_auto_off(10, 0));
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_step_auto_off(0, 0));
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_auto_off(255, 0));
    TEST_ASSERT_EQUAL_UINT8(10, puck_settings_step_auto_off(11, 0));
    TEST_ASSERT_EQUAL_UINT8(15, puck_settings_step_auto_off(14, 0));
    TEST_ASSERT_EQUAL_UINT8(2, puck_settings_step_auto_off(3, 0));
    TEST_ASSERT_EQUAL_UINT8(5, puck_settings_step_auto_off(4, 0));
    TEST_ASSERT_EQUAL_UINT8(30, puck_settings_step_auto_off(37, 0));
    TEST_ASSERT_EQUAL_UINT8(45, puck_settings_step_auto_off(52, 0));
    // 25 is equidistant from 20 and 30 (the only exact tie): the smaller wins.
    TEST_ASSERT_EQUAL_UINT8(20, puck_settings_step_auto_off(25, 0));
}

// One dial step from (enabled, minutes); checks the result.
static void check_dial(bool enabled, uint8_t minutes, int dir, bool want_enabled, uint8_t want_minutes)
{
    bool out_enabled = !want_enabled;
    uint8_t out_minutes = 0;
    puck_settings_step_auto_off_dial(enabled, minutes, dir, &out_enabled, &out_minutes);
    TEST_ASSERT_EQUAL(want_enabled, out_enabled);
    TEST_ASSERT_EQUAL_UINT8(want_minutes, out_minutes);
}

static void test_auto_off_dial_off_and_first_stop(void)
{
    check_dial(false, 10, 1, true, 1);    // + from Off: on at 1 min
    check_dial(false, 45, 1, true, 1);
    check_dial(true, 1, -1, false, 1);    // - from 1: Off, minutes kept
    check_dial(false, 10, 0, false, 10);
}

static void test_auto_off_dial_steps_through_choices(void)
{
    const uint8_t stops[] = { 1, 2, 5, 10, 15, 20, 30, 45, 60 };
    for (size_t i = 0; i + 1 < sizeof(stops); i++) {
        check_dial(true, stops[i], 1, true, stops[i + 1]);
        check_dial(true, stops[i + 1], -1, true, stops[i]);
    }
}

static void test_auto_off_dial_wraps(void)
{
    check_dial(true, 60, 1, false, 60);   // + at 60: Off, minutes kept
    check_dial(true, 60, 5, false, 60);   // any positive dir is one stop
    check_dial(false, 10, -1, true, 60);  // - at Off: 60
    check_dial(false, 1, -5, true, 60);
    check_dial(true, 10, 0, true, 10);
}

static void test_auto_off_dial_snaps_non_choices(void)
{
    check_dial(true, 7, 0, true, 5);
    check_dial(true, 7, 1, true, 10);
    check_dial(true, 0, -1, false, 1);   // snaps to 1, then - goes Off
    check_dial(true, 200, 1, false, 60);  // snaps to 60, then wraps to Off
    check_dial(false, 7, 0, false, 5);
}

static void test_auto_off_dial_index(void)
{
    TEST_ASSERT_EQUAL_UINT8(0, puck_settings_auto_off_dial_index(false, 10));
    TEST_ASSERT_EQUAL_UINT8(0, puck_settings_auto_off_dial_index(false, 60));
    TEST_ASSERT_EQUAL_UINT8(1, puck_settings_auto_off_dial_index(true, 1));
    TEST_ASSERT_EQUAL_UINT8(4, puck_settings_auto_off_dial_index(true, 10));
    TEST_ASSERT_EQUAL_UINT8(PUCK_AUTO_OFF_DIAL_STOPS - 1, puck_settings_auto_off_dial_index(true, 60));
    TEST_ASSERT_EQUAL_UINT8(3, puck_settings_auto_off_dial_index(true, 6));  // nearest: 5
}

static void test_step_timer_up_and_down(void)
{
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_timer(55, 1));
    TEST_ASSERT_EQUAL_UINT8(50, puck_settings_step_timer(55, -1));
    TEST_ASSERT_EQUAL_UINT8(55, puck_settings_step_timer(55, 0));
    TEST_ASSERT_EQUAL_UINT8(55, puck_settings_step_timer(50, 100));
    TEST_ASSERT_EQUAL_UINT8(45, puck_settings_step_timer(50, -100));
}

static void test_step_timer_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MAX_MINUTES, puck_settings_step_timer(PUCK_TIMER_MAX_MINUTES, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MAX_MINUTES, puck_settings_step_timer(PUCK_TIMER_MAX_MINUTES - 5, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MIN_MINUTES, puck_settings_step_timer(PUCK_TIMER_MIN_MINUTES, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MIN_MINUTES, puck_settings_step_timer(PUCK_TIMER_MIN_MINUTES + 5, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MIN_MINUTES, puck_settings_step_timer(0, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MAX_MINUTES, puck_settings_step_timer(255, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MIN_MINUTES, puck_settings_step_timer(0, 0));
    TEST_ASSERT_EQUAL_UINT8(PUCK_TIMER_MAX_MINUTES, puck_settings_step_timer(255, 0));
}

static void test_is_valid_brightness_range_and_step(void)
{
    puck_settings_t s = settings;
    for (unsigned b = PUCK_BRIGHTNESS_MIN; b <= PUCK_BRIGHTNESS_MAX; b += PUCK_BRIGHTNESS_STEP) {
        s.brightness = (uint8_t)b;
        TEST_ASSERT_TRUE(puck_settings_is_valid(&s));
    }
    uint8_t bad[] = { 0, 5, 55, 105, 110, 255 };
    for (size_t i = 0; i < sizeof(bad); i++) {
        s.brightness = bad[i];
        TEST_ASSERT_FALSE(puck_settings_is_valid(&s));
    }
}

static void test_step_brightness_up_and_down(void)
{
    TEST_ASSERT_EQUAL_UINT8(70, puck_settings_step_brightness(60, 1));
    TEST_ASSERT_EQUAL_UINT8(50, puck_settings_step_brightness(60, -1));
    TEST_ASSERT_EQUAL_UINT8(60, puck_settings_step_brightness(60, 0));
}

static void test_step_brightness_clamps(void)
{
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MAX, puck_settings_step_brightness(PUCK_BRIGHTNESS_MAX, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MAX, puck_settings_step_brightness(PUCK_BRIGHTNESS_MAX - 10, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MIN, puck_settings_step_brightness(PUCK_BRIGHTNESS_MIN, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MIN, puck_settings_step_brightness(PUCK_BRIGHTNESS_MIN + 10, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MIN, puck_settings_step_brightness(0, -1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MAX, puck_settings_step_brightness(255, 1));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MIN, puck_settings_step_brightness(0, 0));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BRIGHTNESS_MAX, puck_settings_step_brightness(255, 0));
}

static void expect_default_colors(const puck_settings_t *s)
{
    for (int i = 0; i < PUCK_MAX_PLAYERS; i++) {
        TEST_ASSERT_EQUAL_HEX32(0x000000, s->player_background[i]);
        TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, s->player_text[i]);
    }
}

static void test_encode_matches_v6_fixture(void)
{
    settings.battery_display = PUCK_BATTERY_DISPLAY_ICON_PERCENT;
    settings.player_background[0] = 0x3D91BC;
    settings.player_text[0] = 0xFFFFFF;
    settings.player_background[1] = 0xD6AC48;
    settings.player_text[1] = 0x000000;
    settings.timer_enabled = false;
    settings.brightness = 30;
    settings.p2_same_way = true;
    TEST_ASSERT_EQUAL(28, puck_settings_encode(&settings, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(V6_SAME_WAY_FIXTURE, buf, sizeof(V6_SAME_WAY_FIXTURE));
}

static void test_v6_fixture_decodes(void)
{
    puck_settings_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V6_SAME_WAY_FIXTURE, sizeof(V6_SAME_WAY_FIXTURE), &out));
    TEST_ASSERT_TRUE(out.p2_same_way);
    TEST_ASSERT_EQUAL_UINT8(30, out.brightness);
    TEST_ASSERT_FALSE(out.timer_enabled);
    TEST_ASSERT_EQUAL_HEX32(0xD6AC48, out.player_background[1]);
}

static void test_v5_fixture_decodes(void)
{
    puck_settings_t out;
    memset(&out, 0xff, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V5_BRIGHTNESS_FIXTURE, sizeof(V5_BRIGHTNESS_FIXTURE), &out));
    TEST_ASSERT_EQUAL_UINT8(30, out.brightness);
    TEST_ASSERT_FALSE(out.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON_PERCENT, out.battery_display);
    TEST_ASSERT_EQUAL_HEX32(0x3D91BC, out.player_background[0]);
    TEST_ASSERT_EQUAL_HEX32(0x000000, out.player_text[1]);
    TEST_ASSERT_FALSE(out.p2_same_way);  // added in v6: faces across
}

static void test_v4_fixture_decodes(void)
{
    puck_settings_t out;
    memset(&out, 0xff, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V4_TIMER_OFF_FIXTURE, sizeof(V4_TIMER_OFF_FIXTURE), &out));
    TEST_ASSERT_FALSE(out.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(60, out.brightness);  // added in v5
    TEST_ASSERT_FALSE(out.p2_same_way);
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON_PERCENT, out.battery_display);
    TEST_ASSERT_EQUAL_HEX32(0x3D91BC, out.player_background[0]);
    TEST_ASSERT_EQUAL_HEX32(0x000000, out.player_text[1]);
}

static void test_v3_fixture_decodes(void)
{
    puck_settings_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V3_COLORS_FIXTURE, sizeof(V3_COLORS_FIXTURE), &out));
    TEST_ASSERT_TRUE(out.auto_off_enabled);
    TEST_ASSERT_EQUAL_UINT8(10, out.auto_off_minutes);
    TEST_ASSERT_EQUAL_UINT8(55, out.timer_minutes);
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON_PERCENT, out.battery_display);
    TEST_ASSERT_EQUAL_HEX32(0x3D91BC, out.player_background[0]);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFF, out.player_text[0]);
    TEST_ASSERT_EQUAL_HEX32(0xD6AC48, out.player_background[1]);
    TEST_ASSERT_EQUAL_HEX32(0x000000, out.player_text[1]);
    TEST_ASSERT_TRUE(out.timer_enabled);  // added in v4
    TEST_ASSERT_EQUAL_UINT8(60, out.brightness);
}

static void test_v2_fixtures_decode_with_default_colors(void)
{
    puck_settings_t out;
    memset(&out, 0xff, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V2_DEFAULTS_FIXTURE, sizeof(V2_DEFAULTS_FIXTURE), &out));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON, out.battery_display);
    expect_default_colors(&out);
    TEST_ASSERT_TRUE(out.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(60, out.brightness);
    memset(&out, 0xff, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK,
                      puck_settings_decode(V2_PERCENT_FIXTURE, sizeof(V2_PERCENT_FIXTURE), &out));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON_PERCENT, out.battery_display);
    expect_default_colors(&out);
    TEST_ASSERT_TRUE(out.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(60, out.brightness);
}

static void test_v1_battery_off_decodes_as_off(void)
{
    uint8_t v1[13];
    memcpy(v1, V1_FIXTURE, sizeof(v1));
    v1[8] = 0;
    refresh_crc(v1, sizeof(v1));
    puck_settings_t out;
    TEST_ASSERT_EQUAL(PUCK_STATE_OK, puck_settings_decode(v1, sizeof(v1), &out));
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_OFF, out.battery_display);
}

static void test_is_valid_rejects_bad_battery_display(void)
{
    settings.battery_display = PUCK_BATTERY_DISPLAY_ICON_PERCENT + 1;
    TEST_ASSERT_FALSE(puck_settings_is_valid(&settings));
}

static void test_v1_fixture_decodes(void)
{
    puck_settings_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK, puck_settings_decode(V1_FIXTURE, sizeof(V1_FIXTURE), &out));
    TEST_ASSERT_TRUE(out.auto_off_enabled);
    TEST_ASSERT_EQUAL_UINT8(10, out.auto_off_minutes);
    TEST_ASSERT_EQUAL_UINT8(55, out.timer_minutes);
    TEST_ASSERT_EQUAL_UINT8(PUCK_BATTERY_DISPLAY_ICON, out.battery_display);  // v1 bool true
    expect_default_colors(&out);
    TEST_ASSERT_TRUE(out.timer_enabled);
    TEST_ASSERT_EQUAL_UINT8(60, out.brightness);
}

static void test_round_trip_non_default(void)
{
    puck_settings_t in = { .auto_off_enabled = false, .auto_off_minutes = 45,
                           .timer_minutes = PUCK_TIMER_MAX_MINUTES,
                           .battery_display = PUCK_BATTERY_DISPLAY_OFF,
                           .player_background = { 0x010203, 0xFFFFFF },
                           .player_text = { 0xABCDEF, 0x102030 },
                           .timer_enabled = false,
                           .brightness = 100,
                           .p2_same_way = true };
    TEST_ASSERT_EQUAL(28, puck_settings_encode(&in, buf, sizeof(buf)));
    puck_settings_t out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_EQUAL(PUCK_STATE_OK, puck_settings_decode(buf, 28, &out));
    TEST_ASSERT_EQUAL_MEMORY(&in, &out, sizeof(in));
}

static void test_encoded_size_fits_max(void)
{
    TEST_ASSERT_TRUE(encoded_len <= PUCK_SETTINGS_ENCODED_MAX);
}

static void test_encode_rejects_small_buffer(void)
{
    uint8_t small[27];
    TEST_ASSERT_EQUAL(0, puck_settings_encode(&settings, small, sizeof(small)));
    TEST_ASSERT_EQUAL(0, puck_settings_encode(&settings, small, 0));
    TEST_ASSERT_EQUAL(28, puck_settings_encode(&settings, buf, 28));
}

static void test_rejects_too_short(void)
{
    for (size_t len = 0; len < 9; len++) {
        expect_rejected(buf, len, PUCK_STATE_ERR_SIZE);
    }
}

static void test_rejects_bad_magic(void)
{
    buf[0] = 'X';
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_MAGIC);
}

static void test_rejects_unknown_version(void)
{
    uint8_t versions[] = { 0, 7, 255 };
    for (size_t i = 0; i < sizeof(versions); i++) {
        buf[4] = versions[i];
        refresh_crc(buf, encoded_len);
        expect_rejected(buf, encoded_len, PUCK_STATE_ERR_VERSION);
    }
}

// Data of version v copied from the current encoding, at a given length.
static void expect_version_at_length_rejected(uint8_t version, size_t len)
{
    uint8_t copy[PUCK_SETTINGS_ENCODED_MAX];
    memcpy(copy, buf, len);
    copy[4] = version;
    refresh_crc(copy, len);
    expect_rejected(copy, len, PUCK_STATE_ERR_SIZE);
}

static void test_rejects_wrong_length(void)
{
    uint8_t longer[29];
    memcpy(longer, buf, encoded_len);
    longer[28] = 0;
    expect_rejected(longer, sizeof(longer), PUCK_STATE_ERR_SIZE);
    // Each older version at the current length.
    for (uint8_t v = 1; v < 6; v++) {
        expect_version_at_length_rejected(v, encoded_len);
    }
    // The current version cut to each older version's length.
    const size_t older[] = { 13, 25, 26, 27 };
    for (size_t i = 0; i < sizeof(older) / sizeof(older[0]); i++) {
        expect_rejected(buf, older[i], PUCK_STATE_ERR_SIZE);
        expect_rejected(V6_SAME_WAY_FIXTURE, older[i], PUCK_STATE_ERR_SIZE);
    }
    expect_rejected(V5_BRIGHTNESS_FIXTURE, 26, PUCK_STATE_ERR_SIZE);
    expect_rejected(V4_TIMER_OFF_FIXTURE, 27, PUCK_STATE_ERR_SIZE);
    expect_rejected(buf, 9, PUCK_STATE_ERR_SIZE);
}

static void test_rejects_corruption(void)
{
    for (size_t i = 5; i < encoded_len; i++) {
        uint8_t copy[28];
        memcpy(copy, buf, sizeof(copy));
        copy[i] ^= 0x01;
        expect_rejected(copy, sizeof(copy), PUCK_STATE_ERR_CRC);
    }
}

static void test_rejects_non_boolean_bytes(void)
{
    buf[5] = 2;
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    setUp();
    buf[8] = PUCK_BATTERY_DISPLAY_ICON_PERCENT + 1;  // v2: out of range
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    uint8_t v1[13];
    memcpy(v1, V1_FIXTURE, sizeof(v1));
    v1[8] = 2;  // v1: battery byte must be a bool
    refresh_crc(v1, sizeof(v1));
    expect_rejected(v1, sizeof(v1), PUCK_STATE_ERR_INVALID);
    setUp();
    buf[encoded_len - 7] = 2;  // v4+: timer_enabled must be 0 or 1
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    setUp();
    buf[encoded_len - 5] = 2;  // v6: p2_same_way must be 0 or 1
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
}

static void test_rejects_invalid_brightness(void)
{
    uint8_t bad[] = { 0, 5, 55, 105, 255 };
    for (size_t i = 0; i < sizeof(bad); i++) {
        setUp();
        buf[encoded_len - 6] = bad[i];  // v5: brightness
        refresh_crc(buf, encoded_len);
        expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    }
}

static void test_rejects_invalid_minutes(void)
{
    buf[6] = 3;
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    setUp();
    buf[7] = 52;
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
    setUp();
    buf[7] = 185;
    refresh_crc(buf, encoded_len);
    expect_rejected(buf, encoded_len, PUCK_STATE_ERR_INVALID);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_is_valid_rejects_bad_auto_off_minutes);
    RUN_TEST(test_is_valid_accepts_every_auto_off_choice);
    RUN_TEST(test_is_valid_timer_range_and_step);
    RUN_TEST(test_step_auto_off_up);
    RUN_TEST(test_step_auto_off_down);
    RUN_TEST(test_step_auto_off_clamps_at_ends);
    RUN_TEST(test_step_auto_off_from_non_choice);
    RUN_TEST(test_step_auto_off_dir_zero_snaps_to_nearest);
    RUN_TEST(test_auto_off_dial_off_and_first_stop);
    RUN_TEST(test_auto_off_dial_steps_through_choices);
    RUN_TEST(test_auto_off_dial_wraps);
    RUN_TEST(test_auto_off_dial_snaps_non_choices);
    RUN_TEST(test_auto_off_dial_index);
    RUN_TEST(test_step_timer_up_and_down);
    RUN_TEST(test_step_timer_clamps);
    RUN_TEST(test_is_valid_brightness_range_and_step);
    RUN_TEST(test_step_brightness_up_and_down);
    RUN_TEST(test_step_brightness_clamps);
    RUN_TEST(test_is_valid_rejects_out_of_range_colors);
    RUN_TEST(test_encode_matches_v6_fixture);
    RUN_TEST(test_v6_fixture_decodes);
    RUN_TEST(test_v5_fixture_decodes);
    RUN_TEST(test_v4_fixture_decodes);
    RUN_TEST(test_v3_fixture_decodes);
    RUN_TEST(test_v2_fixtures_decode_with_default_colors);
    RUN_TEST(test_v1_battery_off_decodes_as_off);
    RUN_TEST(test_is_valid_rejects_bad_battery_display);
    RUN_TEST(test_v1_fixture_decodes);
    RUN_TEST(test_round_trip_non_default);
    RUN_TEST(test_encoded_size_fits_max);
    RUN_TEST(test_encode_rejects_small_buffer);
    RUN_TEST(test_rejects_too_short);
    RUN_TEST(test_rejects_bad_magic);
    RUN_TEST(test_rejects_unknown_version);
    RUN_TEST(test_rejects_wrong_length);
    RUN_TEST(test_rejects_corruption);
    RUN_TEST(test_rejects_non_boolean_bytes);
    RUN_TEST(test_rejects_invalid_brightness);
    RUN_TEST(test_rejects_invalid_minutes);
    return UNITY_END();
}
