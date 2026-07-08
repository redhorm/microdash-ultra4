#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"
#include "core/race.h"

// Dashboard renderer for Display A — ST7735  160×80
class SceneDashboard {
public:
    explicit SceneDashboard(lgfx::LGFX_Device& disp);
    void init();
    void render(const VehicleState& s, const UiFx& fx, const RunStats& stats);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;

    // Phase screens
    void _renderBoot(const VehicleState& s, const UiFx& fx);
    void _renderLive(const VehicleState& s, const UiFx& fx);
    void _renderFinish(const UiFx& fx, const RunStats& stats);

    // Live-frame building blocks
    void _drawSpeed(int spd);
    void _drawGear(int g, bool drive_auto, const UiFx& fx);
    void _drawRPMBarPct(float pct, bool flash_on);
    void _drawCorners(const VehicleState& s);
    void _drawBottomRow(const VehicleState& s);
    void _drawAlertBanner(const UiFx& fx);
    void _drawMiniBar(int x, int y, int w, int h, float pct, uint16_t col_fill);
    void _drawCheckerStrip(int y, int h, uint32_t ms);
};

#endif // NATIVE_BUILD
