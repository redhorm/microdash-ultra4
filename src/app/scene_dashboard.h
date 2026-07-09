#pragma once
#ifndef NATIVE_BUILD

#include <LovyanGFX.hpp>
#include "core/state.h"
#include "core/race.h"
#include "core/easing.h"
#include "ui_fonts.h"

// Dashboard renderer for Display A — ST7735  160×80
// RC telemetry style: light blue theme, RPM band with 1-7 markers,
// big RPM readout, red gear box, value panels (readability first:
// the 0.96" panel needs 20-30 px glyphs to be legible).
class SceneDashboard {
public:
    explicit SceneDashboard(lgfx::LGFX_Device& disp);
    void init();
    void render(const VehicleState& s, const UiFx& fx, const RunStats& stats);
    void push();

private:
    LGFX_Sprite        _spr;
    lgfx::LGFX_Device& _disp;
    UiFonts            _fonts;

    // Display-value smoothing (logic lives in core/easing.h)
    Follow _speedF;   // speed panel
    Follow _rpmF;     // RPM band, VU-meter attack/release

    // Phase screens
    void _renderBoot(const VehicleState& s, const UiFx& fx);
    void _renderLive(const VehicleState& s, const UiFx& fx,
                     const RunStats& stats);
    void _renderFinish(const VehicleState& s, const UiFx& fx,
                       const RunStats& stats);

    // Building blocks
    void _drawBand(float rpm, bool flash_on, int vib_y);
    void _drawHero(float rpm);
    void _drawGearBox(int g, bool drive_auto, const UiFx& fx);
    void _drawPanel(int x, int y, int w, int h);
    void _drawValueUnit(int x, int y, int w, int h,
                        const char* value, const char* unit);
    void _drawRows(const VehicleState& s, const RunStats& stats);
    void _drawAlertBanner(const UiFx& fx);
    void _drawCheckerStrip(int y, int h, uint32_t ms);
};

#endif // NATIVE_BUILD
