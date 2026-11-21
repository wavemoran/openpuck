#include "puck_touch_input.h"
#include "unity.h"

#define SHOW_MS 1500

static puck_change_tracker_t tracker;

void setUp(void)
{
    puck_change_tracker_init(&tracker, SHOW_MS);
}

void tearDown(void) {}

static void test_top_half_raises_bottom_half_lowers(void)
{
    TEST_ASSERT_EQUAL_INT16(1, puck_touch_delta(0, 240, false));
    TEST_ASSERT_EQUAL_INT16(1, puck_touch_delta(119, 240, false));
    TEST_ASSERT_EQUAL_INT16(-1, puck_touch_delta(120, 240, false));
    TEST_ASSERT_EQUAL_INT16(-1, puck_touch_delta(239, 240, false));
}

static void test_long_press_uses_big_step(void)
{
    TEST_ASSERT_EQUAL_INT16(PUCK_LONG_PRESS_STEP, puck_touch_delta(10, 240, true));
    TEST_ASSERT_EQUAL_INT16(-PUCK_LONG_PRESS_STEP, puck_touch_delta(230, 240, true));
}

static void check_target_way(int32_t x, int32_t y, uint8_t count, bool same_way, bool lp,
                             uint8_t want_player, int16_t want_delta)
{
    uint8_t player = 99;
    int16_t delta = 0;
    TEST_ASSERT_TRUE(puck_touch_target(x, y, 240, 240, count, same_way, lp, &player, &delta));
    TEST_ASSERT_EQUAL_UINT8(want_player, player);
    TEST_ASSERT_EQUAL_INT16(want_delta, delta);
}

static void check_target(int32_t x, int32_t y, uint8_t count, bool lp,
                         uint8_t want_player, int16_t want_delta)
{
    check_target_way(x, y, count, false, lp, want_player, want_delta);
}

static void test_target_one_player_uses_halves(void)
{
    check_target(10, 10, 1, false, 0, 1);
    check_target(230, 119, 1, false, 0, 1);
    check_target(10, 120, 1, false, 0, -1);
    check_target(230, 230, 1, true, 0, -PUCK_LONG_PRESS_STEP);
    check_target(10, 10, 1, true, 0, PUCK_LONG_PRESS_STEP);
}

static void test_target_two_players_quadrants(void)
{
    check_target(50, 200, 2, false, 0, -1);   // bottom left
    check_target(200, 200, 2, false, 0, 1);   // bottom right
    check_target(50, 40, 2, false, 1, 1);     // top left on screen: their right
    check_target(200, 40, 2, false, 1, -1);   // top right on screen: their left
}

static void test_target_two_players_boundaries(void)
{
    check_target(119, 200, 2, false, 0, -1);
    check_target(120, 200, 2, false, 0, 1);
    check_target(119, 40, 2, false, 1, 1);
    check_target(120, 40, 2, false, 1, -1);
    check_target(50, 119, 2, false, 1, 1);    // last row of the top half
    check_target(50, 120, 2, false, 0, -1);   // first row of the bottom half
}

static void test_target_two_players_long_press(void)
{
    check_target(50, 200, 2, true, 0, -PUCK_LONG_PRESS_STEP);
    check_target(200, 200, 2, true, 0, PUCK_LONG_PRESS_STEP);
    check_target(50, 40, 2, true, 1, PUCK_LONG_PRESS_STEP);
    check_target(200, 40, 2, true, 1, -PUCK_LONG_PRESS_STEP);
}

static void test_target_two_players_same_way(void)
{
    check_target_way(50, 200, 2, true, false, 0, -1);   // bottom: unchanged
    check_target_way(200, 200, 2, true, false, 0, 1);
    check_target_way(50, 40, 2, true, false, 1, -1);    // top left: - like P1
    check_target_way(200, 40, 2, true, true, 1, PUCK_LONG_PRESS_STEP);
    check_target_way(119, 40, 2, true, false, 1, -1);
    check_target_way(120, 40, 2, true, false, 1, 1);
    check_target_way(10, 10, 1, true, false, 0, 1);     // 1 player ignores it
}

static void test_target_rejects_bad_player_count(void)
{
    uint8_t player = 7;
    int16_t delta = 7;
    TEST_ASSERT_FALSE(puck_touch_target(10, 10, 240, 240, 0, false, false, &player, &delta));
    TEST_ASSERT_FALSE(puck_touch_target(10, 10, 240, 240, 3, false, false, &player, &delta));
    TEST_ASSERT_EQUAL_UINT8(7, player);
    TEST_ASSERT_EQUAL_INT16(7, delta);
}

static void test_center_inside_and_outside(void)
{
    TEST_ASSERT_TRUE(puck_touch_in_center(120, 120, 240, 240));
    TEST_ASSERT_TRUE(puck_touch_in_center(140, 100, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(0, 0, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(160, 160, 240, 240));   // 40,40: 3200 > 2025
}

static void test_center_radius_is_inclusive(void)
{
    // The four axis directions: exactly on the radius is inside, one more is outside.
    TEST_ASSERT_TRUE(puck_touch_in_center(120 + PUCK_CENTER_RADIUS, 120, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(120 + PUCK_CENTER_RADIUS + 1, 120, 240, 240));
    TEST_ASSERT_TRUE(puck_touch_in_center(120 - PUCK_CENTER_RADIUS, 120, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(120 - PUCK_CENTER_RADIUS - 1, 120, 240, 240));
    TEST_ASSERT_TRUE(puck_touch_in_center(120, 120 + PUCK_CENTER_RADIUS, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(120, 120 + PUCK_CENTER_RADIUS + 1, 240, 240));
    TEST_ASSERT_TRUE(puck_touch_in_center(120, 120 - PUCK_CENTER_RADIUS, 240, 240));
    TEST_ASSERT_FALSE(puck_touch_in_center(120, 120 - PUCK_CENTER_RADIUS - 1, 240, 240));
}

static void test_zone_center(void)
{
    TEST_ASSERT_EQUAL(PUCK_ZONE_CENTER, puck_touch_zone(120, 120, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_CENTER, puck_touch_zone(120 + PUCK_CENTER_RADIUS, 120, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_CENTER, puck_touch_zone(120, 120 - PUCK_CENTER_RADIUS, 240, 240));
}

static void test_zone_quadrants(void)
{
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_LEFT, puck_touch_zone(30, 30, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_RIGHT, puck_touch_zone(210, 30, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_RIGHT, puck_touch_zone(210, 210, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_LEFT, puck_touch_zone(30, 210, 240, 240));
}

static void test_zone_boundary_lines(void)
{
    // x == width/2 is right; y == height/2 is bottom.
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_LEFT, puck_touch_zone(119, 20, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_RIGHT, puck_touch_zone(120, 20, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_LEFT, puck_touch_zone(119, 220, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_RIGHT, puck_touch_zone(120, 220, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_LEFT, puck_touch_zone(20, 119, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_LEFT, puck_touch_zone(20, 120, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_RIGHT, puck_touch_zone(220, 119, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_RIGHT, puck_touch_zone(220, 120, 240, 240));
}

static void test_zone_just_outside_center(void)
{
    const int32_t r = PUCK_CENTER_RADIUS + 1;
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_LEFT, puck_touch_zone(120 - 1, 120 - r, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_TOP_RIGHT, puck_touch_zone(120, 120 - r, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_RIGHT, puck_touch_zone(120 + r, 120, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_LEFT, puck_touch_zone(120 - r, 120, 240, 240));
    TEST_ASSERT_EQUAL(PUCK_ZONE_BOTTOM_RIGHT, puck_touch_zone(120, 120 + r, 240, 240));
}

static void test_angle_axes(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, puck_touch_angle(200, 120, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(90, puck_touch_angle(120, 200, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(180, puck_touch_angle(40, 120, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(270, puck_touch_angle(120, 40, 240, 240));
}

static void test_angle_diagonals(void)
{
    TEST_ASSERT_EQUAL_UINT16(45, puck_touch_angle(180, 180, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(135, puck_touch_angle(60, 180, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(225, puck_touch_angle(60, 60, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(315, puck_touch_angle(180, 60, 240, 240));
}

static void test_angle_rounds_and_wraps(void)
{
    // atan2(1, 80) = 0.72 degrees rounds to 1; atan2(-1, 80) = -0.72 -> 359.28 rounds to 359.
    TEST_ASSERT_EQUAL_UINT16(1, puck_touch_angle(200, 121, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(359, puck_touch_angle(200, 119, 240, 240));
    // atan2(1, 1000) = 0.057 degrees rounds down to 0; atan2(-1, 1000) rounds up to 360 -> 0.
    TEST_ASSERT_EQUAL_UINT16(0, puck_touch_angle(1120, 121, 240, 240));
    TEST_ASSERT_EQUAL_UINT16(0, puck_touch_angle(1120, 119, 240, 240));
}

static void test_angle_center_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, puck_touch_angle(120, 120, 240, 240));
}

static void test_tracker_hidden_initially(void)
{
    TEST_ASSERT_FALSE(puck_change_tracker_visible(&tracker, 0));
}

static void test_tracker_accumulates_a_run(void)
{
    TEST_ASSERT_EQUAL_INT32(1, puck_change_tracker_add(&tracker, 1, 0));
    TEST_ASSERT_EQUAL_INT32(2, puck_change_tracker_add(&tracker, 1, 500));
    TEST_ASSERT_EQUAL_INT32(-3, puck_change_tracker_add(&tracker, -5, 1000));
    TEST_ASSERT_TRUE(puck_change_tracker_visible(&tracker, 1000 + SHOW_MS - 1));
}

static void test_tracker_expires_and_starts_new_run(void)
{
    puck_change_tracker_add(&tracker, 1, 0);
    puck_change_tracker_add(&tracker, 1, 100);
    TEST_ASSERT_FALSE(puck_change_tracker_visible(&tracker, 100 + SHOW_MS));
    TEST_ASSERT_EQUAL_INT32(-1, puck_change_tracker_add(&tracker, -1, 5000));
}

static void test_tracker_expiry_detected_on_add(void)
{
    puck_change_tracker_add(&tracker, 5, 0);
    TEST_ASSERT_EQUAL_INT32(1, puck_change_tracker_add(&tracker, 1, SHOW_MS + 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_top_half_raises_bottom_half_lowers);
    RUN_TEST(test_long_press_uses_big_step);
    RUN_TEST(test_target_one_player_uses_halves);
    RUN_TEST(test_target_two_players_quadrants);
    RUN_TEST(test_target_two_players_boundaries);
    RUN_TEST(test_target_two_players_long_press);
    RUN_TEST(test_target_two_players_same_way);
    RUN_TEST(test_target_rejects_bad_player_count);
    RUN_TEST(test_center_inside_and_outside);
    RUN_TEST(test_center_radius_is_inclusive);
    RUN_TEST(test_zone_center);
    RUN_TEST(test_zone_quadrants);
    RUN_TEST(test_zone_boundary_lines);
    RUN_TEST(test_zone_just_outside_center);
    RUN_TEST(test_angle_axes);
    RUN_TEST(test_angle_diagonals);
    RUN_TEST(test_angle_rounds_and_wraps);
    RUN_TEST(test_angle_center_is_zero);
    RUN_TEST(test_tracker_hidden_initially);
    RUN_TEST(test_tracker_accumulates_a_run);
    RUN_TEST(test_tracker_expires_and_starts_new_run);
    RUN_TEST(test_tracker_expiry_detected_on_add);
    return UNITY_END();
}
