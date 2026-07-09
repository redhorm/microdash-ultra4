#ifndef NATIVE_BUILD
#include <stdio.h>
#include <string.h>
#include "scene_dashboard.h"
#include "core/formatter.h"
#include "core/anim.h"
#include "core/noise.h"
#include "config.h"

// ── Palette (RGB565) — RC telemetry light theme ─────────────────────────────
static constexpr uint16_t C_SKY   = 0x4D5D; // sky-blue background
static constexpr uint16_t C_PANEL = 0xAEFE; // pale panel background
static constexpr uint16_t C_NAVY  = 0x11D0; // dark navy ink
static constexpr uint16_t C_REDD  = 0xD904; // gear letter / redline
static constexpr uint16_t C_WHITE = 0xFFFF;
static constexpr uint16_t C_CELL  = 0x3BDA; // unlit band cell (mid blue)
static constexpr uint16_t C_DKRED = 0x8082; // alert banner off-pulse

// RPM band gradient, one color per 1000-RPM cell (green → orange)
static constexpr uint16_t C_RAMP[7] = {
    0x5E46, 0x8685, 0xB6E5, 0xE684, 0xF5A3, 0xF4A3, 0xF383
};

// ── Layout (160 × 80) ────────────────────────────────────────────────────────
//  y 1..13   RPM band, 7 cells with 1-7 markers
//  y 15..41  hero "5494 RPM" (Font4 26 px) · red gear box right
//  y 44..60  row 1: SPEED · LAP TIME panels
//  y 62..78  row 2: MOTOR · ESC · BATTERY panels
static constexpr int W = 160, H = 80;

static constexpr int BAND_X = 2,  BAND_Y = 1,  BAND_W = 155, BAND_H = 13;
static constexpr int BAND_CELLS = 7;                  // 1000-RPM markers

static constexpr int HERO_X = 4,  HERO_Y = 15;        // Font4 top edge
static constexpr int GBOX_X = 122, GBOX_Y = 15;       // gear box
static constexpr int GBOX_W = 36,  GBOX_H = 27;

static constexpr int ROW1_Y = 44, ROW2_Y = 62, ROW_H = 16;

SceneDashboard::SceneDashboard(lgfx::LGFX_Device& disp)
    : _spr(&disp), _disp(disp) {}

void SceneDashboard::init() {
    _spr.setColorDepth(16);
    _spr.setPsram(false);          // keep in internal RAM → DMA-friendly
    _spr.createSprite(W, H);
}

void SceneDashboard::push() {
    _spr.pushSprite(&_disp, 0, 0);
}

// ── Building blocks ──────────────────────────────────────────────────────────

void SceneDashboard::_drawPanel(int x, int y, int w, int h) {
    _spr.fillRoundRect(x, y, w, h, 3, C_PANEL);
    _spr.drawRoundRect(x, y, w, h, 3, C_NAVY);
}

// Panel + big value (Font2, 16 px) + small unit, centered as a group
void SceneDashboard::_drawValueUnit(int x, int y, int w, int h,
                                    const char* value, const char* unit) {
    _drawPanel(x, y, w, h);
    _spr.setTextSize(1);
    _spr.setFont(&lgfx::fonts::Font2);
    int vw = _spr.textWidth(value);
    _spr.setFont(&lgfx::fonts::Font0);
    int uw = unit[0] ? _spr.textWidth(unit) + 2 : 0;
    int x0 = x + (w - vw - uw) / 2;
    int bl = y + h - 2;

    _spr.setTextDatum(lgfx::BL_DATUM);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextColor(C_NAVY, C_PANEL);
    _spr.drawString(value, x0, bl);
    if (unit[0]) {
        _spr.setFont(&lgfx::fonts::Font0);
        _spr.drawString(unit, x0 + vw + 2, bl - 2);
    }
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// RPM band: 7 gradient zones of 1000 RPM, filled CONTINUOUSLY like a
// real tach needle (the current cell fills partially — all-or-nothing
// cells looked frozen while cruising inside one 1000-RPM zone).
void SceneDashboard::_drawBand(float rpm, bool flash_on, int vib_y) {
    int y = BAND_Y + vib_y;
    int cell_w = BAND_W / BAND_CELLS;   // 22 px

    for (int i = 0; i < BAND_CELLS; i++) {
        int x0 = BAND_X + i * cell_w;
        int w  = cell_w - 1;
        float p = Anim::clamp01((rpm - i * 1000.0f) / 1000.0f);
        int lit = (int)(p * w + 0.5f);
        uint16_t col = (i >= 5 && flash_on) ? C_WHITE : C_RAMP[i];
        if (lit > 0) _spr.fillRect(x0, y, lit, BAND_H, col);
        if (lit < w) _spr.fillRect(x0 + lit, y, w - lit, BAND_H, C_CELL);

        char n[2] = {(char)('1' + i), 0};
        _spr.setFont(&lgfx::fonts::Font0);
        _spr.setTextSize(1);
        _spr.setTextColor(C_NAVY);           // transparent bg over the fill
        _spr.setTextDatum(lgfx::TC_DATUM);
        _spr.drawString(n, x0 + cell_w / 2, y + 3);
    }
    _spr.setTextDatum(lgfx::TL_DATUM);
    _spr.drawRoundRect(BAND_X - 1, y - 1, BAND_W + 2, BAND_H + 2, 2, C_NAVY);
}

void SceneDashboard::_drawHero(float rpm) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)rpm);

    _spr.setTextSize(1);
    _spr.setFont(&lgfx::fonts::Font4);      // 26 px
    _spr.setTextColor(C_NAVY, C_SKY);
    _spr.drawString(buf, HERO_X, HERO_Y);
    int vw = _spr.textWidth(buf);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.drawString("RPM", HERO_X + vw + 5, HERO_Y + 9);
}

// Red gear letter in a white rounded box (reference style)
void SceneDashboard::_drawGearBox(int g, bool drive_auto, const UiFx& fx) {
    char buf[4];
    Fmt::gear(buf, g, drive_auto);

    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);
    _spr.fillRoundRect(GBOX_X, GBOX_Y, GBOX_W, GBOX_H, 5, C_WHITE);
    _spr.drawRoundRect(GBOX_X, GBOX_Y, GBOX_W, GBOX_H, 5,
                       flash_on ? C_REDD : C_NAVY);
    _spr.drawRoundRect(GBOX_X + 1, GBOX_Y + 1, GBOX_W - 2, GBOX_H - 2, 4,
                       flash_on ? C_REDD : C_NAVY);

    // Snap animation on gear change (130% → 100%)
    float scale = Anim::snapScale(fx.gear_snap_p, GEAR_SNAP_SCALE);
    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(scale);
    _spr.setTextColor(C_REDD, C_WHITE);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(buf, GBOX_X + GBOX_W / 2, GBOX_Y + GBOX_H / 2 + 1);
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

void SceneDashboard::_drawRows(const VehicleState& s, const RunStats& stats) {
    char buf[10];

    // Row 1 — SPEED · LAP TIME
    Fmt::speed(buf, _speedF.value, USE_KMH);
    _drawValueUnit(2, ROW1_Y, 52, ROW_H, buf, USE_KMH ? "KMH" : "MPH");

    Fmt::raceTime(buf, stats.stage_ms);
    _drawValueUnit(58, ROW1_Y, 60, ROW_H, buf, "");

    // Row 2 — MOTOR · ESC · BATTERY
    Fmt::tempPlain(buf, s.engine_temp_f, USE_CELSIUS);
    _drawValueUnit(2, ROW2_Y, 50, ROW_H, buf, "MOT");

    Fmt::tempPlain(buf, s.esc_temp_f, USE_CELSIUS);
    _drawValueUnit(55, ROW2_Y, 50, ROW_H, buf, "ESC");

    snprintf(buf, sizeof(buf), "%.1f", s.battery_v);
    _drawValueUnit(108, ROW2_Y, 49, ROW_H, buf, "V");
}

// Slides in with ease-out, fades out via lerp565 — never pops.
void SceneDashboard::_drawAlertBanner(const UiFx& fx) {
    bool active = fx.alert;
    if (!active && fx.alert_gone_ms >= ALERT_OUT_MS) return;

    float in_p = active
        ? Ease::outCubic(Anim::progress(fx.alert_age_ms, 0, ALERT_IN_MS))
        : 1.0f;
    float fade = active
        ? 1.0f
        : 1.0f - Anim::clamp01((float)fx.alert_gone_ms / ALERT_OUT_MS);

    const int y0 = ROW2_Y - 3;
    const int bh = H - y0 - 2;
    int y = y0 + (int)((1.0f - in_p) * (H - y0));

    bool on = Anim::pulse(fx.now_ms, ALERT_PULSE_MS);
    uint16_t bg     = Anim::lerp565(C_SKY, on ? C_REDD : C_DKRED, fade);
    uint16_t border = Anim::lerp565(C_SKY, C_WHITE, fade);
    _spr.fillRoundRect(2, y, W - 4, bh, 3, bg);
    _spr.drawRoundRect(2, y, W - 4, bh, 3, border);

    char msg[32];
    snprintf(msg, sizeof(msg), "%s %s", fx.alert_info, fx.alert_note);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextSize(1);
    _spr.setTextColor(border, bg);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(msg, W / 2, y + bh / 2);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// Scrolling checkered band, navy/white on the light theme
void SceneDashboard::_drawCheckerStrip(int y, int h, uint32_t ms) {
    _spr.fillRect(0, y, W, h, C_WHITE);
    int off = (int)((ms / 80) % (uint32_t)(2 * h));
    for (int x = -2 * h; x < W; x += 2 * h)
        _spr.fillRect(x + off, y, h, h, C_NAVY);
}

// ── Phase screens ────────────────────────────────────────────────────────────

void SceneDashboard::_renderBoot(const VehicleState& s, const UiFx& fx) {
    uint32_t t = fx.phase_ms;

    if (t < BOOT_LOGO_MS) {
        // Wordmark fade-in from the sky background
        float p = Anim::easeOutCubic(Anim::progress(t, 0, BOOT_LOGO_MS));
        _spr.setFont(&lgfx::fonts::Font4);
        _spr.setTextSize(1);
        _spr.setTextColor(Anim::lerp565(C_SKY, C_NAVY, p), C_SKY);
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.drawString("ULTRA4", W / 2, 32);
        if (p > 0.5f) {
            _spr.setFont(&lgfx::fonts::Font0);
            _spr.setTextColor(Anim::lerp565(C_SKY, C_REDD, (p - 0.5f) * 2.0f),
                              C_SKY);
            _spr.drawString("MICRODASH RC", W / 2, 56);
        }
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else if (t < BOOT_LOGO_MS + BOOT_SWEEP_MS) {
        // Gauge check: band sweeps 0→max→0, hero shows all-8s
        float sweep = Anim::triangle(Anim::progress(t, BOOT_LOGO_MS, BOOT_SWEEP_MS));
        _drawBand(sweep * SIM_MAX_RPM, false, 0);
        _drawHero(8888.0f);
        _drawGearBox(8, false, fx);
        _spr.setFont(&lgfx::fonts::Font0);
        _spr.setTextColor(C_NAVY, C_SKY);
        _spr.setTextDatum(lgfx::TC_DATUM);
        _spr.drawString("SELF CHECK", W / 2, ROW2_Y + 4);
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else {
        // Values settle on the real (idle) state + READY pulse
        _renderLive(s, fx, RunStats{});
        if (Anim::pulse(t, 220)) {
            _drawPanel(50, 28, 60, 22);
            _spr.setFont(&lgfx::fonts::Font2);
            _spr.setTextColor(C_NAVY, C_PANEL);
            _spr.setTextDatum(lgfx::MC_DATUM);
            _spr.drawString("READY", W / 2, 39);
            _spr.setTextDatum(lgfx::TL_DATUM);
        }
    }
}

void SceneDashboard::_renderLive(const VehicleState& s, const UiFx& fx,
                                 const RunStats& stats) {
    bool live = fx.phase == Phase::LIVE;
    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);

    // Smoothed display values (core logic)
    _speedF.update(s.speed_mph, fx.now_ms, SPEED_SMOOTH_MS, SPEED_SMOOTH_MS);
    float rpm_band = _rpmF.update(s.rpm, fx.now_ms,
                                  RPM_ATTACK_MS, RPM_RELEASE_MS);
    // Engine-vibration micro-shake, LIVE only, amplitude ∝ RPM
    int vib = live ? Noise::vibPx(fx.now_ms, rpm_band / SIM_MAX_RPM,
                                  VIB_MAX_PX, VIB_PERIOD_MS,
                                  SIM_NOISE_SEED + 7) : 0;

    _drawBand(rpm_band, flash_on, vib);
    _drawHero(s.rpm);                   // raw: RPM jitter keeps it alive
    _drawGearBox(s.gear, s.drive_auto, fx);
    _drawRows(s, stats);

    _drawAlertBanner(fx);

    // Redline: whole-frame border flash red/white
    if (fx.redline) {
        uint16_t bc = flash_on ? C_REDD : C_WHITE;
        _spr.drawRect(0, 0, W, H, bc);
        _spr.drawRect(1, 1, W - 2, H - 2, bc);
    }

    // BOOT→LIVE reveal: horizontal wipe, coordinated with the navigator
    if (live && fx.phase_ms < LIVE_WIPE_MS) {
        int x = (int)(W * Ease::inOutQuad(
                    Anim::progress(fx.phase_ms, 0, LIVE_WIPE_MS)));
        if (x < W) _spr.fillRect(x, 0, W - x, H, C_SKY);
    }
}

void SceneDashboard::_renderFinish(const VehicleState& s, const UiFx& fx,
                                   const RunStats& stats) {
    // Instruments power down in sequence before the result screen
    if (fx.phase_ms < FINISH_SHUTDOWN_MS) {
        float p = (float)fx.phase_ms / FINISH_SHUTDOWN_MS;
        if (p < 0.25f) _drawRows(s, stats);
        if (p < 0.45f) _drawBand(_rpmF.value, false, 0);
        if (p < 0.70f) _drawGearBox(s.gear, s.drive_auto, fx);
        if (p < 0.85f) _drawHero(s.rpm);
        return;
    }

    _drawCheckerStrip(0, 8, fx.now_ms);
    _drawCheckerStrip(H - 8, 8, fx.now_ms);

    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(1);
    _spr.setTextColor(C_NAVY, C_SKY);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString("FINISH", W / 2, 12);
    _spr.setTextDatum(lgfx::TL_DATUM);

    char buf[10];
    Fmt::speed(buf, stats.max_speed_mph, USE_KMH);
    _drawValueUnit(6, 46, 70, 22, buf, USE_KMH ? "KMH MAX" : "MPH MAX");
    Fmt::rpm(buf, stats.max_rpm);
    _drawValueUnit(84, 46, 70, 22, buf, "RPM");
}

// ── Dispatcher ───────────────────────────────────────────────────────────────

void SceneDashboard::render(const VehicleState& s, const UiFx& fx,
                            const RunStats& stats) {
    _spr.fillScreen(C_SKY);
    switch (fx.phase) {
        case Phase::BOOT:   _renderBoot(s, fx);               break;
        case Phase::FINISH: _renderFinish(s, fx, stats);      break;
        case Phase::LIVE:   _renderLive(s, fx, stats);        break;
    }
}

#endif // NATIVE_BUILD
