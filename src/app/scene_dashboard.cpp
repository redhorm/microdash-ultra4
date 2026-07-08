#ifndef NATIVE_BUILD
#include <stdio.h>
#include "scene_dashboard.h"
#include "core/formatter.h"
#include "config.h"

// ── Palette (RGB565) — Jeep off-road dark theme ─────────────────────────────
static constexpr uint16_t C_BG     = 0x0000; // black
static constexpr uint16_t C_WHITE  = 0xFFFF;
static constexpr uint16_t C_LIME   = 0x9FE0; // lime-green accent
static constexpr uint16_t C_YELLOW = 0xFFE0; // gear / caution
static constexpr uint16_t C_RED    = 0xF800; // danger
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
static constexpr int GEAR_X   = 128;
static constexpr int GEAR_Y   = 20;

static constexpr int RPM_Y    = 66;
static constexpr int RPM_H    = 6;
static constexpr int RPM_X    = 2;
static constexpr int RPM_SEGS = 26;   // segments
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

// ── Sub-renderers ────────────────────────────────────────────────────────────

void SceneDashboard::_drawMiniBar(int x, int y, int w, int h, float pct,
                                  uint16_t col_fill) {
    if (pct < 0) pct = 0;
    if (pct > 1) pct = 1;
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

    // Unit label, small, left of the number
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.drawString(USE_KMH ? "km/h" : "MPH", 2, 56);
}

void SceneDashboard::_drawGear(int g, bool drive_auto) {
    char buf[4];
    Fmt::gear(buf, g, drive_auto);

    _spr.setFont(&lgfx::fonts::Font4);  // 26 px
    _spr.setTextSize(1);
    _spr.setTextColor(C_YELLOW, C_BG);
    _spr.drawString(buf, GEAR_X, GEAR_Y);

    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.drawString(drive_auto ? "AUTO" : "MAN", GEAR_X, GEAR_Y + 28);
}

void SceneDashboard::_drawRPMBar(float rpm) {
    float pct = rpm / SIM_MAX_RPM;
    int   active = (int)(pct * RPM_SEGS + 0.5f);
    if (active > RPM_SEGS) active = RPM_SEGS;

    for (int i = 0; i < RPM_SEGS; i++) {
        int sx = RPM_X + i * (SEG_W + SEG_GAP);
        uint16_t base;
        if      (i < RPM_SEGS * 60 / 100) base = C_GREEN;
        else if (i < RPM_SEGS * 80 / 100) base = C_YELLOW;
        else                              base = C_RED;
        _spr.fillRect(sx, RPM_Y, SEG_W, RPM_H, (i < active) ? base : C_DIM);
    }
}

void SceneDashboard::_drawInfoRow(const VehicleState& s) {
    // Top-left corner: battery voltage + mini-bar
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

    // Top-right corner: engine temperature + mini-bar
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

    // Heading — left
    char hbuf[10];
    Fmt::headingFull(hbuf, s.heading_deg);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.drawString(hbuf, 2, BOTTOM_Y);

    // RPM value — center, dim
    char rbuf[8];
    Fmt::rpm(rbuf, s.rpm);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString(rbuf, SPEED_CX, BOTTOM_Y);

    // 4WD status — right
    _spr.setTextColor(s.four_wd_lock ? C_LIME : C_DIM, C_BG);
    _spr.setTextDatum(lgfx::TR_DATUM);
    _spr.drawString(s.four_wd_lock ? "4WD LOCK" : "4WD AUTO", W - 2, BOTTOM_Y);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Main render ──────────────────────────────────────────────────────────────

void SceneDashboard::render(const VehicleState& s) {
    _spr.fillScreen(C_BG);
    _spr.drawFastHLine(0, 0, W, C_LIME);

    int disp_speed = USE_KMH ? (int)(s.speed_mph * 1.60934f) : (int)s.speed_mph;
    _drawSpeed(disp_speed);
    _drawGear(s.gear, s.drive_auto);
    _drawRPMBar(s.rpm);
    _drawInfoRow(s);
    _drawBottomRow(s);
}

#endif // NATIVE_BUILD
