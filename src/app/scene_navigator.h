#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"
#include "core/roadbook.h"
#include "core/race.h"

// Navigator renderer for Display B — ST7789  240×240
// WAY MAP style: header / roadbook table + speed column / footer
class SceneNavigator {
public:
    SceneNavigator(lgfx::LGFX_Device& disp, const Roadbook& rb);
    void init();
    void render(const VehicleState& s, const UiFx& fx, const RunStats& stats);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;
    const Roadbook&    _rb;

    // Phase screens
    void _renderBoot(const UiFx& fx);
    void _renderLive(const VehicleState& s, const UiFx& fx);
    void _renderFinish(const UiFx& fx, const RunStats& stats);

    // Live-frame building blocks
    void _drawHeader(const UiFx& fx);
    void _drawTable(const VehicleState& s, const UiFx& fx);
    void _drawRow(int y, const Waypoint& wp, bool active, bool alert_on);
    void _drawSideColumn(const VehicleState& s);
    void _drawFooter(const VehicleState& s, const UiFx& fx);
    void _drawArrow(int cx, int cy, const Waypoint& wp);
    void _checkerBand(int y, int h, uint32_t ms);

    // Anti-aliased helpers (LovyanGFX wedge-line primitives)
    void _aaLine(int x0, int y0, int x1, int y1, uint16_t color);
    void _aaArc(int cx, int cy, int r, int a0_deg, int a1_deg, uint16_t color);
    void _aaTri(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color);
};

#endif // NATIVE_BUILD
