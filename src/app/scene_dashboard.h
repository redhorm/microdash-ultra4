#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"

// Dashboard renderer for Display A — ST7735  160×80
class SceneDashboard {
public:
    explicit SceneDashboard(lgfx::LGFX_Device& disp);
    void init();
    void render(const VehicleState& s);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;

    void _drawSpeed(int spd);
    void _drawGear(int g, bool drive_auto);
    void _drawRPMBar(float rpm);
    void _drawInfoRow(const VehicleState& s);
    void _drawBottomRow(const VehicleState& s);
    void _drawMiniBar(int x, int y, int w, int h, float pct, uint16_t col_fill);
};

#endif // NATIVE_BUILD
