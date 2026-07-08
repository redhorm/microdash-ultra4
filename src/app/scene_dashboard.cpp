#ifndef NATIVE_BUILD
#include <stdio.h>
#include "scene_dashboard.h"
#include "core/formatter.h"
#include "core/anim.h"
#include "core/noise.h"
#include "core/compass.h"
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
//  y 1..13    compass tape (top center) · corner texts left/right
//  y 12..16   corner mini-bars
//  y 14..65   speed (VLW 44 px) centered at x=76 · gear column x≥128
//  y 66..72   RPM segment bar
//  y 73..80   bottom row: heading readout · rpm value · 4WD LOCK
static constexpr int W = 160, H = 80;

static constexpr int CORNER_TXT_Y = 2;
static constexpr int CORNER_BAR_Y = 12;
static constexpr int CORNER_BAR_H = 4;
static constexpr int CORNER_BAR_W = 38;

static constexpr int TAPE_X = 44;   // compass tape strip
static constexpr int TAPE_W = 72;
static constexpr int TAPE_Y = 1;
static constexpr int TAPE_H = 13;

static constexpr int SPEED_CX = 76; // speed number center (TC datum)
static constexpr int SPEED_Y  = 14;
static constexpr int GEAR_CX  = 138;
static constexpr int GEAR_CY  = 33;

static constexpr int RPM_Y    = 66;
static constexpr int RPM_H    = 6;
static constexpr int RPM_X    = 2;
static constexpr int RPM_SEGS = 26;
static constexpr int SEG_W    = 5;
static constexpr int SEG_GAP  = 1;

static constexpr int BOTTOM_Y = 73;

static constexpr int BANNER_Y = 58;   // alert banner rest position
static constexpr int BANNER_H = 20;

SceneDashboard::SceneDashboard(lgfx::LGFX_Device& disp)
    : _spr(&disp), _disp(disp) {}

void SceneDashboard::init() {
    _spr.setColorDepth(16);
    _spr.setPsram(false);          // keep in internal RAM → DMA-friendly
    _spr.createSprite(W, H);
    _fonts.init();
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

    _spr.setFont(&_fonts.speed);
    _spr.setTextSize(1);
    _spr.setTextColor(C_WHITE);          // AA-blended over black
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

    _spr.setFont(&_fonts.med);
    _spr.setTextSize(scale);
    _spr.setTextColor(flash_on ? C_WHITE : C_YELLOW);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(buf, GEAR_CX, GEAR_CY);
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::TL_DATUM);

    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString(drive_auto ? "AUTO" : "MAN", GEAR_CX, GEAR_CY + 20);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

void SceneDashboard::_drawRPMBarPct(float pct, bool flash_on, int vib_y) {
    int active = (int)(Anim::clamp01(pct) * RPM_SEGS + 0.5f);
    int y = RPM_Y + vib_y;

    for (int i = 0; i < RPM_SEGS; i++) {
        int sx = RPM_X + i * (SEG_W + SEG_GAP);
        uint16_t base;
        if      (i < RPM_SEGS * 60 / 100) base = C_GREEN;
        else if (i < RPM_SEGS * 80 / 100) base = C_YELLOW;
        else                              base = flash_on ? C_WHITE : C_RED;
        _spr.fillRect(sx, y, SEG_W, RPM_H, (i < active) ? base : C_DIM);
    }
}

void SceneDashboard::_drawCorners(const VehicleState& s, int vib_y) {
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
    _drawMiniBar(2, CORNER_BAR_Y + vib_y, CORNER_BAR_W, CORNER_BAR_H, bpct,
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
    _drawMiniBar(W - 2 - CORNER_BAR_W, CORNER_BAR_Y + vib_y, CORNER_BAR_W,
                 CORNER_BAR_H, tpct,
                 (tpct > 0.8f) ? C_RED : (tpct > 0.55f) ? C_ORANGE : C_GREEN);
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

// Hero detail: aviation-style scrolling compass tape (top center)
void SceneDashboard::_drawTape(float heading_deg) {
    Compass::Tick ticks[12];
    int n = Compass::tape(heading_deg, TAPE_W, TAPE_PX_PER_DEG, ticks, 12);

    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);
    for (int i = 0; i < n; i++) {
        int x = TAPE_X + ticks[i].x;
        if (ticks[i].major) {
            _spr.drawFastVLine(x, TAPE_Y + 1, 5, C_WHITE);
            _spr.setTextColor(C_WHITE, C_BG);
            _spr.setTextDatum(lgfx::TC_DATUM);
            _spr.drawString(ticks[i].label, x, TAPE_Y + 6);
        } else {
            _spr.drawFastVLine(x, TAPE_Y + 1, 3, C_LABEL);
        }
    }
    _spr.setTextDatum(lgfx::TL_DATUM);
    // fixed center marker
    _spr.fillRect(TAPE_X + TAPE_W / 2 - 1, TAPE_Y, 2, 6, C_LIME);
}

// Slides in with ease-out, fades out through lerp565 — never pops.
void SceneDashboard::_drawAlertBanner(const UiFx& fx) {
    bool  active = fx.alert;
    if (!active && fx.alert_gone_ms >= ALERT_OUT_MS) return;

    float in_p = active
        ? Ease::outCubic(Anim::progress(fx.alert_age_ms, 0, ALERT_IN_MS))
        : 1.0f;
    float fade = active
        ? 1.0f
        : 1.0f - Anim::clamp01((float)fx.alert_gone_ms / ALERT_OUT_MS);

    int y = BANNER_Y + (int)((1.0f - in_p) * (H - BANNER_Y));

    bool on = Anim::pulse(fx.now_ms, ALERT_PULSE_MS);
    uint16_t bg     = Anim::lerp565(C_BG, on ? C_RED : C_DKRED, fade);
    uint16_t border = Anim::lerp565(C_BG, C_WHITE, fade);
    _spr.fillRoundRect(2, y, W - 4, BANNER_H, 3, bg);
    _spr.drawRoundRect(2, y, W - 4, BANNER_H, 3, border);

    char msg[32];
    snprintf(msg, sizeof(msg), "%s %s", fx.alert_info, fx.alert_note);
    _spr.setFont(&_fonts.small);
    _spr.setTextSize(1);
    _spr.setTextColor(border);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(msg, W / 2, y + BANNER_H / 2);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// Scrolling checkered band (bg already black: white squares only)
void SceneDashboard::_drawCheckerStrip(int y, int h, uint32_t ms) {
    int off = (int)((ms / 80) % (uint32_t)(2 * h));
    for (int x = -2 * h; x < W; x += 2 * h)
        _spr.fillRect(x + off, y, h, h, C_WHITE);
}

// ── Phase screens ────────────────────────────────────────────────────────────

void SceneDashboard::_renderBoot(const VehicleState& s, const UiFx& fx) {
    uint32_t t = fx.phase_ms;

    if (t < BOOT_LOGO_MS) {
        float p = Anim::easeOutCubic(Anim::progress(t, 0, BOOT_LOGO_MS));
        _spr.setFont(&_fonts.speed);
        _spr.setTextSize(1);
        _spr.setTextColor(Anim::lerp565(C_BG, C_LIME, p));
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.drawString("ULTRA4", W / 2, 34);
        if (p > 0.5f) {
            _spr.setFont(&lgfx::fonts::Font0);
            _spr.setTextColor(Anim::lerp565(C_BG, C_LABEL, (p - 0.5f) * 2.0f), C_BG);
            _spr.drawString("MICRODASH", W / 2, 64);
        }
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else if (t < BOOT_LOGO_MS + BOOT_SWEEP_MS) {
        // Gauge check: RPM sweep 0→max→0, all-8s speed, bars fill
        float sweep = Anim::triangle(Anim::progress(t, BOOT_LOGO_MS, BOOT_SWEEP_MS));

        _spr.drawFastHLine(0, 0, W, C_LIME);
        _spr.setFont(&_fonts.speed);
        _spr.setTextSize(1);
        _spr.setTextColor(C_DIM);
        _spr.setTextDatum(lgfx::TC_DATUM);
        _spr.drawString("888", SPEED_CX, SPEED_Y);
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.setFont(&_fonts.med);
        _spr.drawString("8", GEAR_CX, GEAR_CY);
        _spr.setTextDatum(lgfx::TL_DATUM);

        _drawRPMBarPct(sweep, false, 0);
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
            _spr.setFont(&_fonts.med);
            _spr.setTextColor(C_LIME);
            _spr.setTextDatum(lgfx::MC_DATUM);
            _spr.drawString("READY", W / 2, 40);
            _spr.setTextDatum(lgfx::TL_DATUM);
        }
    }
}

void SceneDashboard::_renderLive(const VehicleState& s, const UiFx& fx) {
    bool live = fx.phase == Phase::LIVE;
    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);

    // Smoothed display values (core logic)
    float spd_f = _speedF.update(s.speed_mph, fx.now_ms,
                                 SPEED_SMOOTH_MS, SPEED_SMOOTH_MS);
    float rpm_f = _rpmF.update(s.rpm / SIM_MAX_RPM, fx.now_ms,
                               RPM_ATTACK_MS, RPM_RELEASE_MS);
    // Engine-vibration micro-shake, LIVE only, amplitude ∝ RPM
    int vib = live ? Noise::vibPx(fx.now_ms, rpm_f, VIB_MAX_PX,
                                  VIB_PERIOD_MS, SIM_NOISE_SEED + 7) : 0;

    _spr.drawFastHLine(0, 0, W, C_LIME);

    int disp_speed = USE_KMH ? (int)(spd_f * 1.60934f) : (int)spd_f;
    _drawSpeed(disp_speed);
    _drawGear(s.gear, s.drive_auto, fx);
    _drawRPMBarPct(rpm_f, flash_on, vib);
    _drawCorners(s, vib);
    _drawBottomRow(s);
    _drawTape(s.heading_deg);

    _drawAlertBanner(fx);

    // Redline: whole-frame border flash red/white
    if (fx.redline) {
        uint16_t bc = flash_on ? C_RED : C_WHITE;
        _spr.drawRect(0, 0, W, H, bc);
        _spr.drawRect(1, 1, W - 2, H - 2, bc);
    }

    // BOOT→LIVE reveal: horizontal wipe, coordinated with the navigator
    if (live && fx.phase_ms < LIVE_WIPE_MS) {
        int x = (int)(W * Ease::inOutQuad(
                    Anim::progress(fx.phase_ms, 0, LIVE_WIPE_MS)));
        if (x < W) _spr.fillRect(x, 0, W - x, H, C_BG);
    }
}

void SceneDashboard::_renderFinish(const VehicleState& s, const UiFx& fx,
                                   const RunStats& stats) {
    // Instruments power down in sequence before the result screen
    if (fx.phase_ms < FINISH_SHUTDOWN_MS) {
        float p = (float)fx.phase_ms / FINISH_SHUTDOWN_MS;
        _spr.drawFastHLine(0, 0, W, C_LIME);
        if (p < 0.25f) _drawCorners(s, 0);
        if (p < 0.40f) {
            _drawRPMBarPct(_rpmF.value, false, 0);
            _drawBottomRow(s);
        }
        if (p < 0.55f) _drawTape(s.heading_deg);
        if (p < 0.70f) _drawGear(s.gear, s.drive_auto, fx);
        if (p < 0.85f) _drawSpeed((int)_speedF.value);
        return;
    }

    _drawCheckerStrip(0, 8, fx.now_ms);
    _drawCheckerStrip(H - 8, 8, fx.now_ms);

    _spr.setFont(&_fonts.med);
    _spr.setTextSize(1);
    _spr.setTextColor(C_LIME);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString("FINISH", W / 2, 10);

    char buf[10];
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LABEL, C_BG);
    _spr.drawString("MAX SPEED", 42, 36);
    _spr.drawString("MAX RPM", 118, 36);

    _spr.setFont(&_fonts.med);
    _spr.setTextColor(C_WHITE);
    Fmt::speed(buf, stats.max_speed_mph, USE_KMH);
    _spr.drawString(buf, 42, 46);
    _spr.setTextColor(C_YELLOW);
    Fmt::rpm(buf, stats.max_rpm);
    _spr.drawString(buf, 118, 46);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Dispatcher ───────────────────────────────────────────────────────────────

void SceneDashboard::render(const VehicleState& s, const UiFx& fx,
                            const RunStats& stats) {
    _spr.fillScreen(C_BG);
    switch (fx.phase) {
        case Phase::BOOT:   _renderBoot(s, fx);            break;
        case Phase::FINISH: _renderFinish(s, fx, stats);   break;
        case Phase::LIVE:   _renderLive(s, fx);            break;
    }
}

#endif // NATIVE_BUILD
