#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"
#include "core/race.h"
#include "core/easing.h"

// Dashboard renderer for Display A — ST7735  160×80 landscape
// RC racing style (light blue): gear bar 1-7 on top, racing swoosh,
// dominant centered RPM, red gear box, GEAR/LAP row, MOTOR/ESC/VOLT row.
// Built-in bitmap fonts only (VLW renders corrupted on this panel).
class SceneDashboard {
public:
    explicit SceneDashboard(lgfx::LGFX_Device& disp);
    void init();
    void render(const VehicleState& s, const UiFx& fx, const RunStats& stats);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;

    Follow _rpmF;   // RPM bar smoothing, VU-meter attack/release

    // Phase screens
    void _renderBoot(const VehicleState& s, const UiFx& fx);
    void _renderLive(const VehicleState& s, const UiFx& fx,
                     const RunStats& stats);
    void _renderFinish(const VehicleState& s, const UiFx& fx,
                       const RunStats& stats);

    // Layout building blocks (all coordinates in Lay:: constants)
    void _drawFrame();
    void _drawRPMBar(float rpm, bool flash_on);
    void _drawSwoosh();
    void _drawRPM(float rpm);
    void _drawGearBox(int g, bool drive_auto, const UiFx& fx);
    void _drawMetricBox(int x, int y, int w, int h,
                        const char* value, const char* label,
                        bool degree = false);
    void _drawRows(const VehicleState& s, const RunStats& stats);
    void _drawAlertBanner(const UiFx& fx);
    void _drawCheckerStrip(int y, int h, uint32_t ms);

    // Text helper: centers txt inside (x,y,w,h) with the given font
    void _drawCenteredText(const char* txt, int x, int y, int w, int h,
                           const lgfx::IFont* font, uint16_t color);
};

#endif // NATIVE_BUILD
