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
#include "core/driver.h"
#include "core/race.h"
#include "core/anim.h"

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

static void test_roadbook_finish_detection() {
    // Lap looping is now owned by RaceController: past the finish the
    // roadbook just reports the last waypoint as active/finished.
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 12.10f;          // past FINISH @ 12.05
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(rb.getCount() - 1, s.active_wp);
    TEST_ASSERT_TRUE(rb.isFinished());
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.dist_to_next);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 12.10f, s.odo_mi);  // odo untouched
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

// ── Driver (roadbook-aware autopilot) ────────────────────────────────────────

static void test_driver_brakes_into_danger_waypoint() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 5.64f;            // 0.03 mi before WP5 (L HAIRPIN @ 5.67)
    rb.update(s);
    TEST_ASSERT_EQUAL_INT(4, s.active_wp);
    s.speed_mph = 60.0f;

    Driver drv;
    DriveInputs in = drv.compute(s, rb);
    TEST_ASSERT_TRUE(in.brake > 0.5f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, in.throttle);
}

static void test_driver_accelerates_on_straight() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 6.0f;             // 2.34 mi of room before WP6 (R 90 @ 8.34)
    rb.update(s);
    s.speed_mph = 30.0f;

    Driver drv;
    DriveInputs in = drv.compute(s, rb);
    TEST_ASSERT_TRUE(in.throttle > 0.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, in.brake);
}

static void test_driver_holds_corner_speed_at_apex() {
    Roadbook rb;
    VehicleState s;
    s.odo_mi = 5.655f;           // 0.015 mi before the hairpin, already slow
    rb.update(s);
    s.speed_mph = 11.0f;         // just under DRV_TGT_HAIRPIN_MPH

    Driver drv;
    DriveInputs in = drv.compute(s, rb);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, in.brake);
}

static void test_driver_corner_speed_mapping() {
    TEST_ASSERT_FLOAT_WITHIN(0.1f, DRV_TGT_HAIRPIN_MPH,
                             Driver::cornerSpeed(WaypointDir::HAIRPIN_L));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, DRV_TGT_90_MPH,
                             Driver::cornerSpeed(WaypointDir::RIGHT_90));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, DRV_TGT_WATER_MPH,
                             Driver::cornerSpeed(WaypointDir::WATER));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, DRV_CRUISE_MPH,
                             Driver::cornerSpeed(WaypointDir::FINISH));
}

// ── RaceController ───────────────────────────────────────────────────────────

// Steps the controller in DASH_FRAME_MS increments, returns final time
static uint32_t stepRace(RaceController& rc, Simulator& sim, Roadbook& rb,
                         uint32_t from_ms, uint32_t duration_ms) {
    uint32_t now = from_ms;
    uint32_t end = from_ms + duration_ms;
    while (now < end) {
        now += DASH_FRAME_MS;
        rc.update(now, sim, rb);
    }
    return now;
}

static void test_race_boot_then_live() {
    Simulator sim;  sim.reset();
    Roadbook rb;    rb.reset();
    RaceController rc;
    rc.reset(1000);

    rc.update(1000 + DASH_FRAME_MS, sim, rb);
    TEST_ASSERT_TRUE(rc.fx().phase == Phase::BOOT);

    stepRace(rc, sim, rb, 1000 + DASH_FRAME_MS, BOOT_TOTAL_MS + 100);
    TEST_ASSERT_TRUE(rc.fx().phase == Phase::LIVE);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, sim.state.odo_mi);
}

static void test_race_full_lap_finish_and_restart() {
    Simulator sim;  sim.reset();
    Roadbook rb;    rb.reset();
    RaceController rc;
    rc.reset(0);

    // Run up to 30 simulated minutes; the demo driver must complete
    // the 12.05 mi stage well within that.
    uint32_t now = stepRace(rc, sim, rb, 0, BOOT_TOTAL_MS + 100);
    bool finished = false;
    for (int i = 0; i < 30 * 60000 / DASH_FRAME_MS && !finished; i++) {
        now += DASH_FRAME_MS;
        rc.update(now, sim, rb);
        finished = rc.fx().phase == Phase::FINISH;
    }
    TEST_ASSERT_TRUE(finished);
    TEST_ASSERT_TRUE(rc.stats().max_speed_mph > 30.0f);
    TEST_ASSERT_TRUE(rc.stats().max_rpm > 4000.0f);
    TEST_ASSERT_TRUE(rc.stats().stage_ms > 60000);

    // After the hold the lap restarts from zero
    stepRace(rc, sim, rb, now, FINISH_HOLD_MS + 200);
    TEST_ASSERT_TRUE(rc.fx().phase == Phase::LIVE);
    TEST_ASSERT_TRUE(sim.state.odo_mi < 0.5f);
    TEST_ASSERT_EQUAL_INT(0, sim.state.active_wp);
    TEST_ASSERT_TRUE(rc.stats().stage_ms < 5000);
}

static void test_race_alert_near_danger_waypoint() {
    Simulator sim;  sim.reset();
    Roadbook rb;    rb.reset();
    RaceController rc;
    rc.reset(0);
    uint32_t now = stepRace(rc, sim, rb, 0, BOOT_TOTAL_MS + 100);

    // Far from any danger waypoint → no alert
    sim.state.odo_mi = 0.50f;
    now += DASH_FRAME_MS;
    rc.update(now, sim, rb);
    TEST_ASSERT_FALSE(rc.fx().alert);

    // 0.12 mi before WP2 (L 90 CAUTION @ 1.23, danger) → alert on
    sim.state.odo_mi = 1.11f;
    now += DASH_FRAME_MS;
    rc.update(now, sim, rb);
    TEST_ASSERT_TRUE(rc.fx().alert);
    TEST_ASSERT_EQUAL_INT(2, rc.fx().alert_wp);
    TEST_ASSERT_TRUE(rc.fx().alert_dist <= ALERT_DIST_MI);
    TEST_ASSERT_EQUAL_STRING("L 90", rc.fx().alert_info);
}

static void test_race_gear_snap_progress() {
    Simulator sim;  sim.reset();
    Roadbook rb;    rb.reset();
    RaceController rc;
    rc.reset(0);
    uint32_t now = stepRace(rc, sim, rb, 0, BOOT_TOTAL_MS + 100);

    // Drive until the first gear change happens
    float p_at_change = 1.0f;
    for (int i = 0; i < 20000 / DASH_FRAME_MS; i++) {
        now += DASH_FRAME_MS;
        rc.update(now, sim, rb);
        if (sim.state.gear > 1) { p_at_change = rc.fx().gear_snap_p; break; }
    }
    TEST_ASSERT_TRUE(sim.state.gear > 1);
    TEST_ASSERT_TRUE(p_at_change < 1.0f);   // snap animation just started

    // Well past GEAR_SNAP_MS (and no new change within a couple frames)
    now += GEAR_SNAP_MS + 3 * DASH_FRAME_MS;
    rc.update(now, sim, rb);
    int g = sim.state.gear;
    now += DASH_FRAME_MS;
    rc.update(now, sim, rb);
    if (sim.state.gear == g)
        TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rc.fx().gear_snap_p);
}

// ── Anim helpers ─────────────────────────────────────────────────────────────

static void test_anim_progress() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, Anim::progress(100, 100, 200));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, Anim::progress(200, 100, 200));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, Anim::progress(400, 100, 200));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, Anim::progress(50, 100, 0));
}

static void test_anim_pulse() {
    TEST_ASSERT_TRUE(Anim::pulse(0, 100));
    TEST_ASSERT_TRUE(Anim::pulse(99, 100));
    TEST_ASSERT_FALSE(Anim::pulse(100, 100));
    TEST_ASSERT_TRUE(Anim::pulse(200, 100));
}

static void test_anim_triangle_and_snap() {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, Anim::triangle(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, Anim::triangle(0.5f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, Anim::triangle(1.0f));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, GEAR_SNAP_SCALE,
                             Anim::snapScale(0.0f, GEAR_SNAP_SCALE));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
                             Anim::snapScale(1.0f, GEAR_SNAP_SCALE));
    // monotonically decreasing
    float prev = GEAR_SNAP_SCALE;
    for (float t = 0.1f; t <= 1.0f; t += 0.1f) {
        float v = Anim::snapScale(t, GEAR_SNAP_SCALE);
        TEST_ASSERT_TRUE(v <= prev + 0.001f);
        prev = v;
    }
}

static void test_anim_lerp565() {
    TEST_ASSERT_EQUAL_INT(0x0000, Anim::lerp565(0x0000, 0xFFFF, 0.0f));
    TEST_ASSERT_EQUAL_INT(0xFFFF, Anim::lerp565(0x0000, 0xFFFF, 1.0f));
    uint16_t mid = Anim::lerp565(0x0000, 0xFFFF, 0.5f);
    int r = (mid >> 11) & 31, g = (mid >> 5) & 63, b = mid & 31;
    TEST_ASSERT_TRUE(r >= 14 && r <= 16);
    TEST_ASSERT_TRUE(g >= 30 && g <= 32);
    TEST_ASSERT_TRUE(b >= 14 && b <= 16);
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

static void test_fmt_race_time() {
    char b[10];
    Fmt::raceTime(b, 0);
    TEST_ASSERT_EQUAL_STRING("00:00.0", b);
    Fmt::raceTime(b, 263700);            // 4 min 23.7 s
    TEST_ASSERT_EQUAL_STRING("04:23.7", b);
    Fmt::raceTime(b, 59950);
    TEST_ASSERT_EQUAL_STRING("00:59.9", b);
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_roadbook_initial_state);
    RUN_TEST(test_roadbook_advances_waypoint);
    RUN_TEST(test_roadbook_skips_multiple_waypoints);
    RUN_TEST(test_roadbook_finish_detection);
    RUN_TEST(test_roadbook_reset);
    RUN_TEST(test_roadbook_stage_data_consistency);

    RUN_TEST(test_sim_reset_defaults);
    RUN_TEST(test_sim_zero_dt_is_noop);
    RUN_TEST(test_sim_accelerates_from_standstill);
    RUN_TEST(test_sim_gears_follow_speed);
    RUN_TEST(test_sim_values_stay_in_bounds);
    RUN_TEST(test_sim_odometer_advances);
    RUN_TEST(test_sim_dt_clamp);

    RUN_TEST(test_driver_brakes_into_danger_waypoint);
    RUN_TEST(test_driver_accelerates_on_straight);
    RUN_TEST(test_driver_holds_corner_speed_at_apex);
    RUN_TEST(test_driver_corner_speed_mapping);

    RUN_TEST(test_race_boot_then_live);
    RUN_TEST(test_race_full_lap_finish_and_restart);
    RUN_TEST(test_race_alert_near_danger_waypoint);
    RUN_TEST(test_race_gear_snap_progress);

    RUN_TEST(test_anim_progress);
    RUN_TEST(test_anim_pulse);
    RUN_TEST(test_anim_triangle_and_snap);
    RUN_TEST(test_anim_lerp565);

    RUN_TEST(test_fmt_speed);
    RUN_TEST(test_fmt_battery);
    RUN_TEST(test_fmt_temp);
    RUN_TEST(test_fmt_heading);
    RUN_TEST(test_fmt_dist);
    RUN_TEST(test_fmt_gear);
    RUN_TEST(test_fmt_percent);
    RUN_TEST(test_fmt_clock);
    RUN_TEST(test_fmt_race_time);

    return UNITY_END();
}
