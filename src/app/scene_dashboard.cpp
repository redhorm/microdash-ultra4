#ifndef NATIVE_BUILD
#include <stdio.h>
#include "scene_dashboard.h"
#include "core/formatter.h"
#include "core/anim.h"
#include "config.h"

// ── Palette (RGB565) — Jeep off-road dark theme ─────────────────────────────
static constexpr uint16_t C_BG     = 0x0000; // black
static constexpr uint16_t C_WHITE  = 0xFFFF;
static constexpr uint16_t C_LIME   = 0x9FE0; // lime-green accent
static constexpr uint16_t C_YELLOW = 0xFFE0; // gear / caution
static constexpr uint16_t C_RED    = 0xF800; // danger
static constexpr uint16_t C_DKRED  = 0x6000; // alert banner off-pulse
static constexpr uint16_t C_GREEN  = 0x07E0; // bar good zone
static constexpr uint16_t C_ORANGE = 0xFC60; // temp warning
static constexpr uint16_t C_DIM    = 0x2104; // inactive bar segment
static constexpr uint16_t C_LABEL  = 0x6B6D; // dim labels

// ── Layout (160 × 80) ────────────────────────────────────────────────────────
//  y 0        lime accent line
//  y 2..10    corners: battery text (left) · temp text (right)
//  y 12..16   corner mini-bars
//  y 17..65   speed Font7 (48 px) centered at x=76 · gear column x≥128
//  y 66..72   RPM segment bar
//  y 73..80   bottom row: heading · rpm value · 4WD LOCK
static constexpr int W = 160, H = 80;

static constexpr int CORNER_TXT_Y = 2;
static constexpr int CORNER_BAR_Y = 12;
static constexpr int CORNER_BAR_H = 4;
static constexpr int CORNER_BAR_W = 38;

static constexpr int SPEED_CX = 76;   // speed number center (TC datum)
static constexpr int SPEED_Y  = 17;
static constexpr int GEAR_CX  = 138;  // gear glyph center (MC datum, snap anim)
static constexpr int GEAR_CY  = 33;

static constexpr int RPM_Y    = 66;
static constexpr int RPM_H    = 6;
static constexpr int RPM_X    = 2;
static constexpr int RPM_SEGS = 26;
static constexpr int SEG_W    = 5;
static constexpr int SEG_GAP  = 1;

static constexpr int BOTTOM_Y = 73;

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

void SceneDashboard::_drawMiniBar(int x, int y, int w, int h, float pct,
                                  uint16_t col_fill) {
    pct = Anim::clamp01(pct);
    _spr.fillRect(x, y, w, h, C_DIM);
    int fill = (int)(pct * w);
    if (fill > 0) _spr.fillRect(x, y, fill, h, col_fill);
    _spr.drawRect(x, y, w, h, C_LABEL);
}

void SceneDashboard::_drawSpeed(int spd) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", spd);

    _spr.setFont(&lgfx::fonts::Font7); // 7-segment, 48 px tall
    _spr.setTextSize(1);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString(buf, SPEED_CX, SPEED_Y);
    _spr.setTextDatum(lgfx::TL_DATUM);

    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.drawString(USE_KMH ? "km/h" : "MPH", 2, 56);
}

void SceneDashboard::_drawGear(int g, bool drive_auto, const UiFx& fx) {
    char buf[4];
    Fmt::gear(buf, g, drive_auto);

    // Snap animation on gear change (130% → 100%), white flash at redline
    float scale = Anim::snapScale(fx.gear_snap_p, GEAR_SNAP_SCALE);
    bool  flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);

    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(scale);
    _spr.setTextColor(flash_on ? C_WHITE : C_YELLOW, C_BG);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(buf, GEAR_CX, GEAR_CY);
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::TL_DATUM);

    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString(drive_auto ? "AUTO" : "MAN", GEAR_CX, GEAR_CY + 28);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

void SceneDashboard::_drawRPMBarPct(float pct, bool flash_on) {
    int active = (int)(Anim::clamp01(pct) * RPM_SEGS + 0.5f);

    for (int i = 0; i < RPM_SEGS; i++) {
        int sx = RPM_X + i * (SEG_W + SEG_GAP);
        uint16_t base;
        if      (i < RPM_SEGS * 60 / 100) base = C_GREEN;
        else if (i < RPM_SEGS * 80 / 100) base = C_YELLOW;
        else                              base = flash_on ? C_WHITE : C_RED;
        _spr.fillRect(sx, RPM_Y, SEG_W, RPM_H, (i < active) ? base : C_DIM);
    }
}

void SceneDashboard::_drawCorners(const VehicleState& s) {
    // Top-left: battery voltage + mini-bar
    char bbuf[8];
    Fmt::battery(bbuf, s.battery_v);
    uint16_t bcol = (s.battery_v < 11.8f) ? C_RED
                  : (s.battery_v < 12.8f) ? C_YELLOW
                  :                         C_LIME;
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);
    _spr.setTextColor(bcol, C_BG);
    _spr.drawString(bbuf, 2, CORNER_TXT_Y);
    float bpct = (s.battery_v - 10.0f) / 6.0f;   // 10–16 V → 0–1
    _drawMiniBar(2, CORNER_BAR_Y, CORNER_BAR_W, CORNER_BAR_H, bpct,
                 (bpct < 0.3f) ? C_RED : (bpct < 0.6f) ? C_YELLOW : C_GREEN);

    // Top-right: engine temperature + mini-bar
    char tbuf[8];
    Fmt::temp(tbuf, s.engine_temp_f, USE_CELSIUS);
    uint16_t tcol = (s.engine_temp_f > 240.0f) ? C_RED
                  : (s.engine_temp_f > 210.0f) ? C_ORANGE
                  :                              C_WHITE;
    _spr.setTextColor(tcol, C_BG);
    _spr.setTextDatum(lgfx::TR_DATUM);
    _spr.drawString(tbuf, W - 2, CORNER_TXT_Y);
    _spr.setTextDatum(lgfx::TL_DATUM);
    float tpct = (s.engine_temp_f - 140.0f) / 125.0f; // 140–265 °F → 0–1
    _drawMiniBar(W - 2 - CORNER_BAR_W, CORNER_BAR_Y, CORNER_BAR_W, CORNER_BAR_H,
                 tpct, (tpct > 0.8f) ? C_RED : (tpct > 0.55f) ? C_ORANGE : C_GREEN);
}

void SceneDashboard::_drawBottomRow(const VehicleState& s) {
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);

    char hbuf[10];
    Fmt::headingFull(hbuf, s.heading_deg);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.drawString(hbuf, 2, BOTTOM_Y);

    char rbuf[8];
    Fmt::rpm(rbuf, s.rpm);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString(rbuf, SPEED_CX, BOTTOM_Y);

    _spr.setTextColor(s.four_wd_lock ? C_LIME : C_DIM, C_BG);
    _spr.setTextDatum(lgfx::TR_DATUM);
    _spr.drawString(s.four_wd_lock ? "4WD LOCK" : "4WD AUTO", W - 2, BOTTOM_Y);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

void SceneDashboard::_drawAlertBanner(const UiFx& fx) {
    bool on = Anim::pulse(fx.now_ms, ALERT_PULSE_MS);
    _spr.fillRoundRect(2, 58, W - 4, 20, 3, on ? C_RED : C_DKRED);
    _spr.drawRoundRect(2, 58, W - 4, 20, 3, C_WHITE);

    char msg[32];
    snprintf(msg, sizeof(msg), "%s %s", fx.alert_info, fx.alert_note);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextSize(1);
    _spr.setTextColor(C_WHITE, on ? C_RED : C_DKRED);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(msg, W / 2, 68);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// Scrolling checkered band (background is already black: draw the
// white squares only, offset animated by ms)
void SceneDashboard::_drawCheckerStrip(int y, int h, uint32_t ms) {
    int off = (int)((ms / 80) % (uint32_t)(2 * h));
    for (int x = -2 * h; x < W; x += 2 * h)
        _spr.fillRect(x + off, y, h, h, C_WHITE);
}

// ── Phase screens ────────────────────────────────────────────────────────────

void SceneDashboard::_renderBoot(const VehicleState& s, const UiFx& fx) {
    uint32_t t = fx.phase_ms;

    if (t < BOOT_LOGO_MS) {
        // Wordmark fade-in
        float p = Anim::easeOutCubic(Anim::progress(t, 0, BOOT_LOGO_MS));
        _spr.setFont(&lgfx::fonts::Font4);
        _spr.setTextSize(1);
        _spr.setTextColor(Anim::lerp565(C_BG, C_LIME, p), C_BG);
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.drawString("ULTRA4", W / 2, 32);
        if (p > 0.5f) {
            _spr.setFont(&lgfx::fonts::Font0);
            _spr.setTextColor(Anim::lerp565(C_BG, C_LABEL, (p - 0.5f) * 2.0f), C_BG);
            _spr.drawString("MICRODASH", W / 2, 54);
        }
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else if (t < BOOT_LOGO_MS + BOOT_SWEEP_MS) {
        // Gauge check: RPM sweep 0→max→0, all-segments speed, bars fill
        float sweep = Anim::triangle(Anim::progress(t, BOOT_LOGO_MS, BOOT_SWEEP_MS));

        _spr.drawFastHLine(0, 0, W, C_LIME);
        _spr.setFont(&lgfx::fonts::Font7);
        _spr.setTextSize(1);
        _spr.setTextColor(C_DIM, C_BG);
        _spr.setTextDatum(lgfx::TC_DATUM);
        _spr.drawString("888", SPEED_CX, SPEED_Y);

        _spr.setFont(&lgfx::fonts::Font4);
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.drawString("8", GEAR_CX, GEAR_CY);
        _spr.setTextDatum(lgfx::TL_DATUM);

        _drawRPMBarPct(sweep, false);
        _drawMiniBar(2, CORNER_BAR_Y, CORNER_BAR_W, CORNER_BAR_H, sweep, C_GREEN);
        _drawMiniBar(W - 2 - CORNER_BAR_W, CORNER_BAR_Y, CORNER_BAR_W,
                     CORNER_BAR_H, sweep, C_GREEN);

        _spr.setFont(&lgfx::fonts::Font0);
        _spr.setTextColor(C_LABEL, C_BG);
        _spr.setTextDatum(lgfx::TC_DATUM);
        _spr.drawString("SELF CHECK", SPEED_CX, BOTTOM_Y);
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else {
        // Values settle on the real (idle) state + READY pulse
        _renderLive(s, fx);
        if (Anim::pulse(t, 220)) {
            _spr.setFont(&lgfx::fonts::Font2);
            _spr.setTextColor(C_LIME, C_BG);
            _spr.setTextDatum(lgfx::MC_DATUM);
            _spr.drawString("READY", W / 2, 40);
            _spr.setTextDatum(lgfx::TL_DATUM);
        }
    }
}

void SceneDashboard::_renderLive(const VehicleState& s, const UiFx& fx) {
    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);

    _spr.drawFastHLine(0, 0, W, C_LIME);

    int disp_speed = USE_KMH ? (int)(s.speed_mph * 1.60934f) : (int)s.speed_mph;
    _drawSpeed(disp_speed);
    _drawGear(s.gear, s.drive_auto, fx);
    _drawRPMBarPct(s.rpm / SIM_MAX_RPM, flash_on);
    _drawCorners(s);
    _drawBottomRow(s);

    if (fx.alert) _drawAlertBanner(fx);

    // Redline: whole-frame border flash red/white
    if (fx.redline) {
        uint16_t bc = flash_on ? C_RED : C_WHITE;
        _spr.drawRect(0, 0, W, H, bc);
        _spr.drawRect(1, 1, W - 2, H - 2, bc);
    }
}

void SceneDashboard::_renderFinish(const UiFx& fx, const RunStats& stats) {
    _drawCheckerStrip(0, 8, fx.now_ms);
    _drawCheckerStrip(H - 8, 8, fx.now_ms);

    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextSize(1);
    _spr.setTextColor(C_LIME, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString("FINISH", W / 2, 11);

    char buf[10];
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.drawString("MAX SPEED", 42, 32);
    _spr.drawString("MAX RPM", 118, 32);

    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextColor(C_WHITE, C_BG);
    Fmt::speed(buf, stats.max_speed_mph, USE_KMH);
    _spr.drawString(buf, 42, 42);
    _spr.setTextColor(C_YELLOW, C_BG);
    Fmt::rpm(buf, stats.max_rpm);
    _spr.drawString(buf, 118, 42);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Dispatcher ───────────────────────────────────────────────────────────────

void SceneDashboard::render(const VehicleState& s, const UiFx& fx,
                            const RunStats& stats) {
    _spr.fillScreen(C_BG);
    switch (fx.phase) {
        case Phase::BOOT:   _renderBoot(s, fx);          break;
        case Phase::FINISH: _renderFinish(fx, stats);    break;
        case Phase::LIVE:   _renderLive(s, fx);          break;
    }
}

#endif // NATIVE_BUILD
