#include "simulator.h"
#include "config.h"
#include <math.h>

static float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Gear thresholds (mph): shift up above [gear], down below [gear]
static const float UP_MPH[SIM_NUM_GEARS + 1]   = {0, 0, 18, 32, 52, 72};
static const float DOWN_MPH[SIM_NUM_GEARS + 1] = {0, 0,  8, 22, 38, 56};
// Effective wheel-RPM-to-engine-RPM multipliers per gear
static const float GEAR_RATIO[SIM_NUM_GEARS + 1] = {0, 3.6f, 2.2f, 1.5f, 1.1f, 0.8f};

void Simulator::reset() {
    state = VehicleState{};
    _elapsed_ms = 0;
    _throttle   = 0.0f;
    _brake      = 0.0f;
}

void Simulator::update(uint32_t dt_ms) {
    if (dt_ms == 0) return;
    if (dt_ms > 200) dt_ms = 200; // clamp large jumps (first frame / resume)

    _elapsed_ms += dt_ms;
    float dt_s = dt_ms * 0.001f;

    _updateAutopilot();
    _updatePowertrain(dt_s);
    _updateThermal(dt_s);
    _updateNav(dt_s);
}

// ── Autopilot ────────────────────────────────────────────────────────────────
// 8-second cycle: 3s hard accel → 2s cruise → 2s brake → 1s idle
void Simulator::_updateAutopilot() {
    uint32_t t = _elapsed_ms % 8000;
    if (t < 3000) {
        _throttle = 0.88f;
        _brake    = 0.0f;
    } else if (t < 5000) {
        _throttle = 0.28f;
        _brake    = 0.0f;
    } else if (t < 7000) {
        _throttle = 0.0f;
        _brake    = 0.75f;
    } else {
        _throttle = 0.0f;
        _brake    = 0.0f;
    }
}

// ── Powertrain ───────────────────────────────────────────────────────────────
void Simulator::_updatePowertrain(float dt_s) {
    // Speed
    float drag   = state.speed_mph * 0.016f;
    float accel  = _throttle * 9.0f - _brake * 18.0f - drag;
    state.speed_mph += accel * dt_s;
    state.speed_mph = clampf(state.speed_mph, 0.0f, SIM_MAX_SPEED_MPH);

    // Gear logic
    if (state.speed_mph >= UP_MPH[state.gear] && state.gear < SIM_NUM_GEARS)
        state.gear++;
    else if (state.speed_mph < DOWN_MPH[state.gear] && state.gear > 1)
        state.gear--;

    // RPM: blend engine speed from wheel speed + direct throttle blip
    float wheel_rpm = state.speed_mph * GEAR_RATIO[state.gear] * 155.0f;
    float blip      = _throttle * 900.0f;
    float target    = (_throttle > 0.05f) ? (wheel_rpm + blip)
                                           : SIM_IDLE_RPM;
    // Low-pass filter for smooth RPM
    float alpha = 1.0f - expf(-dt_s * 4.0f);
    state.rpm += (target - state.rpm) * alpha;
    state.rpm = clampf(state.rpm, SIM_IDLE_RPM * 0.9f, SIM_MAX_RPM);

    // Fuel drain
    state.fuel_pct -= _throttle * 2e-6f * (dt_s * 1000.0f);
    state.fuel_pct  = clampf(state.fuel_pct, 0.0f, 1.0f);

    // Battery: drain under load, slow recharge at light throttle
    float load = _throttle * 0.4f + 0.08f;
    state.battery_v -= load * 1e-4f * (dt_s * 1000.0f);
    if (_throttle > 0.15f && _throttle < 0.75f && state.battery_v < 14.0f)
        state.battery_v += 4e-5f * (dt_s * 1000.0f);
    state.battery_v = clampf(state.battery_v, 11.0f, 14.6f);
}

// ── Thermal ──────────────────────────────────────────────────────────────────
void Simulator::_updateThermal(float dt_s) {
    float target = 155.0f + _throttle * 105.0f + _brake * 18.0f;
    float alpha  = 1.0f - expf(-dt_s * 0.3f);
    state.engine_temp_f += (target - state.engine_temp_f) * alpha;
    state.engine_temp_f  = clampf(state.engine_temp_f, 140.0f, 265.0f);
}

// ── Navigation ───────────────────────────────────────────────────────────────
void Simulator::_updateNav(float dt_s) {
    // Odometer: speed_mph → miles/s
    state.odo_mi += state.speed_mph * dt_s / 3600.0f;

    // Heading rotates slowly (simulated trail turns)
    state.heading_deg += 3.5f * dt_s;
    if (state.heading_deg >= 360.0f) state.heading_deg -= 360.0f;
}
