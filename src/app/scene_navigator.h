#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"
#include "core/roadbook.h"

// Navigator renderer for Display B — ST7789  240×240
// WAY MAP style: header / roadbook table + speed column / footer
class SceneNavigator {
public:
    SceneNavigator(lgfx::LGFX_Device& disp, const Roadbook& rb);
    void init();
    void render(const VehicleState& s);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;
    const Roadbook&    _rb;

    void _drawHeader(const VehicleState& s);
    void _drawTable(const VehicleState& s);
    void _drawRow(int y, const Waypoint& wp, bool active);
    void _drawSideColumn(const VehicleState& s);
    void _drawFooter(const VehicleState& s);

    void _drawArrow(int cx, int cy, const Waypoint& wp);

    // Drawing helpers (portable across LGFX versions)
    void _thickLine(int x0, int y0, int x1, int y1, uint16_t color);
    void _arcLines(int cx, int cy, int r, int a0_deg, int a1_deg, uint16_t color);
    void _thickArc(int cx, int cy, int r, int a0_deg, int a1_deg, uint16_t color);
};

#endif // NATIVE_BUILD
