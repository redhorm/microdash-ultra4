#pragma once
#include "state.h"

class Simulator {
public:
    VehicleState state;

    void reset();
    // dt_ms: milliseconds since last call
    void update(uint32_t dt_ms);

private:
    uint32_t _elapsed_ms = 0;

    void _updateAutopilot();
    void _updatePowertrain(float dt_s);
    void _updateThermal(float dt_s);
    void _updateNav(float dt_s);

    float _throttle = 0.0f;
    float _brake    = 0.0f;
};
