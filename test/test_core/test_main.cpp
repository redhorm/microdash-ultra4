// ============================================================
// Host-compiled unit tests (pio test -e native)
// Covers: roadbook progression, physics simulator, formatting.
// Pure C++ — no Arduino, no LovyanGFX.
// ============================================================
#include <unity.h>
#include <string.h>
#include "config.h"
#include "core/simulator.h"
#include "core/roadbook.h"
#include "core/formatter.h"

void setUp() {}
void tearDown() {}

// ── Roadbook ─────────────────────────────────────────────────────────────────

static void test_roadbook_initial_state() {
    Roadbook rb;
    VehicleState s;
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(0, s.active_wp);
    TEST_ASSERT_EQUAL_INT(9, rb.getCount());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.45f, s.dist_to_next);
    TEST_ASSERT_FALSE(rb.isFinished());
}

static void test_roadbook_advances_waypoint() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 0.50f;           // past WP1 @ 0.45
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(1, s.active_wp);

    s.odo_mi = 1.22f;           // just before WP2 @ 1.23
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(1, s.active_wp);

    s.odo_mi = 1.24f;           // past WP2
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(2, s.active_wp);
}

static void test_roadbook_skips_multiple_waypoints() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 9.0f;            // between WP6 @ 8.34 and WP7 @ 9.78
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(6, s.active_wp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 9.78f - 9.0f, s.dist_to_next);
}

static void test_roadbook_loops_after_finish() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 12.10f;          // past FINISH @ 12.05
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(0, s.active_wp);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.odo_mi);
}

static void test_roadbook_reset() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 6.0f;
    rb.update(s);
    TEST_ASSERT_TRUE(s.active_wp > 0);
    rb.reset();
    TEST_ASSERT_EQUAL_INT(0, rb.getActive());
}

static void test_roadbook_stage_data_consistency() {
    // total_mi must be monotonically increasing and match segment sums
    Roadbook rb;
    float sum = 0.0f;
    for (int i = 0; i < rb.getCount(); i++) {
        sum += rb.get(i).dist_mi;
        TEST_ASSERT_FLOAT_WITHIN(0.005f, rb.get(i).total_mi, sum);
        if (i > 0)
            TEST_ASSERT_TRUE(rb.get(i).total_mi > rb.get(i - 1).total_mi);
    }
}

// ── Simulator ────────────────────────────────────────────────────────────────

static void test_sim_reset_defaults() {
    Simulator sim;
    sim.reset();
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sim.state.speed_mph);
    TEST_ASSERT_EQUAL_INT(1, sim.state.gear);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sim.state.odo_mi);
}

static void test_sim_zero_dt_is_noop() {
    Simulator sim;
    sim.reset();
    VehicleState before = sim.state;
    sim.update(0);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.speed_mph, sim.state.speed_mph);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, before.rpm, sim.state.rpm);
}

static void test_sim_accelerates_from_standstill() {
    Simulator sim;
    sim.reset();
    // 2 s into the autopilot cycle = hard acceleration phase
    for (int i = 0; i < 20; i++) sim.update(100);
    TEST_ASSERT_TRUE(sim.state.speed_mph > 5.0f);
    TEST_ASSERT_TRUE(sim.state.rpm > SIM_IDLE_RPM);
}

static void test_sim_gears_follow_speed() {
    Simulator sim;
    sim.reset();
    int max_gear = 1;
    float max_speed = 0.0f;
    // Two full autopilot cycles (16 s)
    for (int i = 0; i < 160; i++) {
        sim.update(100);
        if (sim.state.gear > max_gear)          max_gear  = sim.state.gear;
        if (sim.state.speed_mph > max_speed)    max_speed = sim.state.speed_mph;
    }
    TEST_ASSERT_TRUE(max_speed > 15.0f);
    TEST_ASSERT_TRUE(max_gear >= 2);
    // After the braking phase speed must come back down near zero
    TEST_ASSERT_TRUE(sim.state.speed_mph < max_speed);
}

static void test_sim_values_stay_in_bounds() {
    Simulator sim;
    sim.reset();
    for (int i = 0; i < 600; i++) {   // 60 s
        sim.update(100);
        TEST_ASSERT_TRUE(sim.state.speed_mph >= 0.0f);
        TEST_ASSERT_TRUE(sim.state.speed_mph <= SIM_MAX_SPEED_MPH);
        TEST_ASSERT_TRUE(sim.state.rpm <= SIM_MAX_RPM);
        TEST_ASSERT_TRUE(sim.state.battery_v >= 11.0f);
        TEST_ASSERT_TRUE(sim.state.battery_v <= 14.6f);
        TEST_ASSERT_TRUE(sim.state.engine_temp_f >= 140.0f);
        TEST_ASSERT_TRUE(sim.state.engine_temp_f <= 265.0f);
        TEST_ASSERT_TRUE(sim.state.gear >= 1);
        TEST_ASSERT_TRUE(sim.state.gear <= SIM_NUM_GEARS);
    }
}

static void test_sim_odometer_advances() {
    Simulator sim;
    sim.reset();
    float prev = 0.0f;
    for (int i = 0; i < 100; i++) {
        sim.update(100);
        TEST_ASSERT_TRUE(sim.state.odo_mi >= prev);   // monotonic
        prev = sim.state.odo_mi;
    }
    TEST_ASSERT_TRUE(sim.state.odo_mi > 0.0f);
}

static void test_sim_dt_clamp() {
    Simulator sim;
    sim.reset();
    sim.update(60000);   // absurd dt (resume after pause) must not explode
    TEST_ASSERT_TRUE(sim.state.speed_mph <= SIM_MAX_SPEED_MPH);
    TEST_ASSERT_TRUE(sim.state.odo_mi < 0.1f);   // clamped to 200 ms
}

// ── Formatter ────────────────────────────────────────────────────────────────

static void test_fmt_speed() {
    char b[8];
    Fmt::speed(b, 45.7f);
    TEST_ASSERT_EQUAL_STRING("45", b);
    Fmt::speed(b, 45.0f, true);          // 45 mph = 72.4 km/h
    TEST_ASSERT_EQUAL_STRING("72", b);
}

static void test_fmt_battery() {
    char b[8];
    Fmt::battery(b, 14.23f);
    TEST_ASSERT_EQUAL_STRING("14.2V", b);
}

static void test_fmt_temp() {
    char b[8];
    Fmt::temp(b, 210.4f);
    TEST_ASSERT_EQUAL_STRING("210\xF8""F", b);
    Fmt::temp(b, 212.0f, true);          // 212 °F = 100 °C
    TEST_ASSERT_EQUAL_STRING("100\xF8""C", b);
}

static void test_fmt_heading() {
    char b[10];
    Fmt::heading(b, 0.0f);    TEST_ASSERT_EQUAL_STRING("N",  b);
    Fmt::heading(b, 90.0f);   TEST_ASSERT_EQUAL_STRING("E",  b);
    Fmt::heading(b, 225.0f);  TEST_ASSERT_EQUAL_STRING("SW", b);
    Fmt::heading(b, 348.0f);  TEST_ASSERT_EQUAL_STRING("N",  b);
    Fmt::heading(b, 300.0f);  TEST_ASSERT_EQUAL_STRING("NW", b);
    Fmt::heading(b, -45.0f);  TEST_ASSERT_EQUAL_STRING("NW", b);
    Fmt::headingFull(b, 348.0f);
    TEST_ASSERT_EQUAL_STRING("N 348", b);
}

static void test_fmt_dist() {
    char b[10];
    Fmt::dist(b, 0.45f);
    TEST_ASSERT_EQUAL_STRING("0.45 mi", b);
    Fmt::distNum(b, 0.45f);
    TEST_ASSERT_EQUAL_STRING("0.45", b);
    Fmt::distNum(b, 1.0f, true);         // 1 mi = 1.61 km
    TEST_ASSERT_EQUAL_STRING("1.61", b);
}

static void test_fmt_gear() {
    char b[4];
    Fmt::gear(b, 3, true);
    TEST_ASSERT_EQUAL_STRING("D", b);
    Fmt::gear(b, 3, false);
    TEST_ASSERT_EQUAL_STRING("3", b);
}

static void test_fmt_percent() {
    char b[6];
    Fmt::percent(b, 0.78f);
    TEST_ASSERT_EQUAL_STRING("78%", b);
}

static void test_fmt_clock() {
    char b[10];
    Fmt::clockHMS(b, 0);
    TEST_ASSERT_EQUAL_STRING("12:00:00", b);
    Fmt::clockHMS(b, 3725);              // +1h 2m 5s
    TEST_ASSERT_EQUAL_STRING("13:02:05", b);
    Fmt::clockHMS(b, 13 * 3600);         // wraps past midnight
    TEST_ASSERT_EQUAL_STRING("01:00:00", b);
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_roadbook_initial_state);
    RUN_TEST(test_roadbook_advances_waypoint);
    RUN_TEST(test_roadbook_skips_multiple_waypoints);
    RUN_TEST(test_roadbook_loops_after_finish);
    RUN_TEST(test_roadbook_reset);
    RUN_TEST(test_roadbook_stage_data_consistency);

    RUN_TEST(test_sim_reset_defaults);
    RUN_TEST(test_sim_zero_dt_is_noop);
    RUN_TEST(test_sim_accelerates_from_standstill);
    RUN_TEST(test_sim_gears_follow_speed);
    RUN_TEST(test_sim_values_stay_in_bounds);
    RUN_TEST(test_sim_odometer_advances);
    RUN_TEST(test_sim_dt_clamp);

    RUN_TEST(test_fmt_speed);
    RUN_TEST(test_fmt_battery);
    RUN_TEST(test_fmt_temp);
    RUN_TEST(test_fmt_heading);
    RUN_TEST(test_fmt_dist);
    RUN_TEST(test_fmt_gear);
    RUN_TEST(test_fmt_percent);
    RUN_TEST(test_fmt_clock);

    return UNITY_END();
}
