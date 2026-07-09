#ifndef NATIVE_BUILD
#include <stdio.h>
#include <string.h>
#include "scene_dashboard.h"
#include "core/formatter.h"
#include "core/anim.h"
#include "config.h"

// ── Palette (RGB565) — racing light-blue theme ──────────────────────────────
namespace Col {
constexpr uint16_t SKY      = 0x4D5D;  // main background
constexpr uint16_t BOX_BG   = 0xAEFE;  // pale box interior
constexpr uint16_t BOX_HI   = 0xD73F;  // gear-box interior (lighter)
constexpr uint16_t INK      = 0x11D0;  // dark navy: values, borders
constexpr uint16_t INK_MID  = 0x2AF5;  // medium blue: labels, box borders
constexpr uint16_t ACCENT   = 0xD904;  // red/orange: big gear letter
constexpr uint16_t SEG_OFF  = 0x3BDA;  // unlit RPM segment (mid blue)
constexpr uint16_t RPM_LOW  = 0x2580;  // RPM gradient start (green)
constexpr uint16_t RPM_MID  = 0xF600;  // RPM gradient middle (yellow)
constexpr uint16_t RPM_HIGH = 0xE082;  // RPM gradient end / limiter (red)
constexpr uint16_t WHITE    = 0xFFFF;
constexpr uint16_t DKRED    = 0x8082;  // alert banner off-pulse
}

// ── Layout constants — canvas 160×80, 2 px safe margin ─────────────────────
namespace Lay {
constexpr int W = 160, H = 80;

// outer frame
constexpr int FRAME_X = 1, FRAME_Y = 1, FRAME_W = 158, FRAME_H = 78, FRAME_R = 4;

// RPM bar: many thin gradient steps (green → yellow → red)
constexpr int BAR_X = 5, BAR_Y = 3, BAR_W = 150, BAR_H = 13;
constexpr int BAR_SEGS = 30;
constexpr int SEG_W = BAR_W / BAR_SEGS;              // 5 px (4 + 1 gap)

// racing swoosh under the bar
constexpr int SWOOSH_Y = 18;

// central hero row: big SPEED + smaller red RPM, shared baseline
// (lowered so it doesn't crowd the bar above)
constexpr int HERO_X = 8, HERO_Y = 25, HERO_H = 22;
constexpr int HERO_R = 108;          // right edge (before the gear box)

// gear box right
constexpr int GBOX_X = 112, GBOX_Y = 21, GBOX_W = 40, GBOX_H = 30, GBOX_R = 4;

// middle row: GEAR / LAP TIME
constexpr int CAR_X = 8,  CAR_Y = 48, CAR_W = 45, CAR_H = 14;
constexpr int LAP_X = 57, LAP_Y = 48, LAP_W = 55, LAP_H = 14;

// bottom row: MOTOR / ESC / VOLTAGE
constexpr int BOT_Y = 63, BOT_H = 14, BOT_W = 48;
constexpr int BOT1_X = 4, BOT2_X = 56, BOT3_X = 108;
constexpr int BOX_R = 3;
}

SceneDashboard::SceneDashboard(lgfx::LGFX_Device& disp)
    : _spr(&disp), _disp(disp) {}

void SceneDashboard::init() {
    _spr.setColorDepth(16);
    _spr.setPsram(false);          // internal RAM → DMA-friendly
    _spr.createSprite(Lay::W, Lay::H);
}

void SceneDashboard::push() {
    _spr.pushSprite(&_disp, 0, 0);
}

// ── Text helper ──────────────────────────────────────────────────────────────

// Centers txt inside the (x,y,w,h) box, transparent background so it
// composes over fills and borders without erasing them.
void SceneDashboard::_drawCenteredText(const char* txt, int x, int y,
                                       int w, int h,
                                       const lgfx::IFont* font,
                                       uint16_t color) {
    _spr.setFont(font);
    _spr.setTextSize(1);
    _spr.setTextColor(color);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(txt, x + w / 2, y + h / 2);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Building blocks ──────────────────────────────────────────────────────────

void SceneDashboard::_drawFrame() {
    _spr.fillScreen(Col::SKY);
    _spr.drawRoundRect(Lay::FRAME_X, Lay::FRAME_Y,
                       Lay::FRAME_W, Lay::FRAME_H, Lay::FRAME_R, Col::INK);
}

// RPM bar: 30 thin segments with a green→yellow→red gradient, lit up
// to the current RPM. At the limiter the WHOLE bar strobes red.
void SceneDashboard::_drawRPMBar(float rpm, bool flash_on) {
    using namespace Lay;
    float pct = Anim::clamp01(rpm / SIM_MAX_RPM);
    int   lit = (int)(pct * BAR_SEGS + 0.5f);
    bool  limiter = rpm >= REDLINE_RPM;

    for (int i = 0; i < BAR_SEGS; i++) {
        int x = BAR_X + i * SEG_W;
        uint16_t col;
        if (limiter) {
            // limiter: full bar flashes red / dark red
            col = flash_on ? Col::RPM_HIGH : Col::DKRED;
        } else if (i < lit) {
            // two-stage gradient across the lit segments
            float t = (float)i / (BAR_SEGS - 1);
            col = (t < 0.5f)
                ? Anim::lerp565(Col::RPM_LOW, Col::RPM_MID, t * 2.0f)
                : Anim::lerp565(Col::RPM_MID, Col::RPM_HIGH, (t - 0.5f) * 2.0f);
        } else {
            col = Col::SEG_OFF;
        }
        _spr.fillRect(x, BAR_Y, SEG_W - 1, BAR_H, col);
    }
    _spr.drawRect(Lay::BAR_X - 1, Lay::BAR_Y - 1,
                  Lay::BAR_W + 2, Lay::BAR_H + 2, Col::INK);
}

// Two thin racing lines under the bar, dipping at the left (mockup style)
void SceneDashboard::_drawSwoosh() {
    using namespace Lay;
    for (int k = 0; k < 2; k++) {
        int y = SWOOSH_Y + k * 2;
        _spr.drawFastHLine(26, y, W - 26 - 4, Col::INK);
        // small curve bending down toward the left edge
        _spr.drawLine(26, y, 12, y + 3, Col::INK);
        _spr.drawLine(12, y + 3, 4, y + 6, Col::INK);
    }
}

// Hero row on one baseline: dominant red RPM (left, the red number
// right under the RPM bar needs no label) + smaller speed with MPH
// unit, right-aligned before the gear box.
void SceneDashboard::_drawHeroRow(float rpm, float speed_mph) {
    using namespace Lay;
    int bl = HERO_Y + HERO_H - 1;               // shared baseline
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::BL_DATUM);

    // RPM — protagonist
    char val[8];
    snprintf(val, sizeof(val), "%d", (int)rpm);
    _spr.setFont(&lgfx::fonts::Font4);          // 26 px
    _spr.setTextColor(Col::ACCENT);
    _spr.drawString(val, HERO_X, bl);

    // speed — secondary, right-aligned
    char sv[8];
    Fmt::speed(sv, speed_mph, USE_KMH);
    const char* unit = USE_KMH ? "KMH" : "MPH";
    _spr.setFont(&lgfx::fonts::Font2);          // 16 px
    int vw = _spr.textWidth(sv);
    _spr.setFont(&lgfx::fonts::Font0);
    int uw = _spr.textWidth(unit);
    int x0 = HERO_R - (vw + 3 + uw);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextColor(Col::INK);
    _spr.drawString(sv, x0, bl);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(Col::INK_MID);
    _spr.drawString(unit, x0 + vw + 3, bl - 2);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// Big gear letter, red on a lighter box (reference style)
void SceneDashboard::_drawGearBox(int g, bool drive_auto, const UiFx& fx) {
    using namespace Lay;
    char buf[4];
    Fmt::gear(buf, g, drive_auto);

    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);
    _spr.fillRoundRect(GBOX_X, GBOX_Y, GBOX_W, GBOX_H, GBOX_R, Col::BOX_HI);
    _spr.drawRoundRect(GBOX_X, GBOX_Y, GBOX_W, GBOX_H, GBOX_R,
                       flash_on ? Col::ACCENT : Col::INK);
    _spr.drawRoundRect(GBOX_X + 1, GBOX_Y + 1, GBOX_W - 2, GBOX_H - 2,
                       GBOX_R - 1, flash_on ? Col::ACCENT : Col::INK);

    // Snap animation on gear change (130% → 100%)
    float scale = Anim::snapScale(fx.gear_snap_p, GEAR_SNAP_SCALE);
    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(scale);
    _spr.setTextColor(Col::ACCENT);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.drawString(buf, GBOX_X + GBOX_W / 2, GBOX_Y + GBOX_H / 2 + 1);
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// Box with centered value; optional tiny label on the left and optional
// degree ring after the value (Font2 has no ° glyph)
void SceneDashboard::_drawMetricBox(int x, int y, int w, int h,
                                    const char* value, const char* label,
                                    bool degree) {
    _spr.fillRoundRect(x, y, w, h, Lay::BOX_R, Col::BOX_BG);
    _spr.drawRoundRect(x, y, w, h, Lay::BOX_R, Col::INK_MID);

    int lx = x, lw = 0;
    if (label && label[0]) {
        _spr.setFont(&lgfx::fonts::Font0);
        lw = _spr.textWidth(label) + 4;
        _spr.setTextSize(1);
        _spr.setTextColor(Col::INK_MID);
        _spr.setTextDatum(lgfx::ML_DATUM);
        _spr.drawString(label, lx + 3, y + h / 2);
        _spr.setTextDatum(lgfx::TL_DATUM);
    }

    // value centered in the remaining width
    _spr.setFont(&lgfx::fonts::Font2);
    int vw = _spr.textWidth(value);
    int vx = x + lw + (w - lw - vw) / 2;
    _spr.setTextSize(1);
    _spr.setTextColor(Col::INK);
    _spr.setTextDatum(lgfx::ML_DATUM);
    _spr.drawString(value, vx, y + h / 2);
    _spr.setTextDatum(lgfx::TL_DATUM);

    if (degree)   // small ° ring at the value's top-right
        _spr.drawCircle(vx + vw + 2, y + h / 2 - 4, 1, Col::INK);
}

void SceneDashboard::_drawRows(const VehicleState& s, const RunStats& stats) {
    using namespace Lay;
    char buf[10];

    // middle row: numeric gear + lap time (labels only where they fit)
    snprintf(buf, sizeof(buf), "%d", s.gear);
    _drawMetricBox(CAR_X, CAR_Y, CAR_W, CAR_H, buf, "GEAR");

    Fmt::raceTime(buf, stats.stage_ms);
    const char* lap = (buf[0] == '0') ? buf + 1 : buf;   // "0:13.4"
    _drawMetricBox(LAP_X, LAP_Y, LAP_W, LAP_H, lap, "");

    // bottom row: motor / esc / battery ("M"/"E": full labels would
    // clip 3-digit temps in a 48 px box)
    snprintf(buf, sizeof(buf), "%d",
             (int)((s.engine_temp_f - 32.0f) * 5.0f / 9.0f));
    _drawMetricBox(BOT1_X, BOT_Y, BOT_W, BOT_H, buf, "M", true);

    snprintf(buf, sizeof(buf), "%d",
             (int)((s.esc_temp_f - 32.0f) * 5.0f / 9.0f));
    _drawMetricBox(BOT2_X, BOT_Y, BOT_W, BOT_H, buf, "E", true);

    snprintf(buf, sizeof(buf), "%.1fV", s.battery_v);
    _drawMetricBox(BOT3_X, BOT_Y, BOT_W, BOT_H, buf, "");
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

    const int y0 = Lay::BOT_Y - 3;
    const int bh = Lay::H - y0 - 2;
    int y = y0 + (int)((1.0f - in_p) * (Lay::H - y0));

    bool on = Anim::pulse(fx.now_ms, ALERT_PULSE_MS);
    uint16_t bg     = Anim::lerp565(Col::SKY, on ? Col::ACCENT : Col::DKRED, fade);
    uint16_t border = Anim::lerp565(Col::SKY, Col::WHITE, fade);
    _spr.fillRoundRect(2, y, Lay::W - 4, bh, Lay::BOX_R, bg);
    _spr.drawRoundRect(2, y, Lay::W - 4, bh, Lay::BOX_R, border);

    char msg[32];
    snprintf(msg, sizeof(msg), "%s %s", fx.alert_info, fx.alert_note);
    _drawCenteredText(msg, 2, y, Lay::W - 4, bh, &lgfx::fonts::Font2, border);
}

// Scrolling checkered band, navy/white on the light theme
void SceneDashboard::_drawCheckerStrip(int y, int h, uint32_t ms) {
    _spr.fillRect(0, y, Lay::W, h, Col::WHITE);
    int off = (int)((ms / 80) % (uint32_t)(2 * h));
    for (int x = -2 * h; x < Lay::W; x += 2 * h)
        _spr.fillRect(x + off, y, h, h, Col::INK);
}

// ── Phase screens ────────────────────────────────────────────────────────────

void SceneDashboard::_renderBoot(const VehicleState& s, const UiFx& fx) {
    uint32_t t = fx.phase_ms;

    if (t < BOOT_LOGO_MS) {
        _drawFrame();
        float p = Anim::easeOutCubic(Anim::progress(t, 0, BOOT_LOGO_MS));
        _spr.setFont(&lgfx::fonts::Font4);
        _spr.setTextSize(1);
        _spr.setTextColor(Anim::lerp565(Col::SKY, Col::INK, p));
        _spr.setTextDatum(lgfx::MC_DATUM);
        _spr.drawString("ULTRA4", Lay::W / 2, 32);
        if (p > 0.5f) {
            _spr.setFont(&lgfx::fonts::Font0);
            _spr.setTextColor(Anim::lerp565(Col::SKY, Col::ACCENT,
                                            (p - 0.5f) * 2.0f));
            _spr.drawString("MICRODASH RC", Lay::W / 2, 56);
        }
        _spr.setTextDatum(lgfx::TL_DATUM);

    } else if (t < BOOT_LOGO_MS + BOOT_SWEEP_MS) {
        // Gauge check: RPM bar sweeps 0→max→0
        _drawFrame();
        float sweep = Anim::triangle(Anim::progress(t, BOOT_LOGO_MS, BOOT_SWEEP_MS));
        _drawRPMBar(sweep * (REDLINE_RPM - 100.0f), false);
        _drawSwoosh();
        _drawHeroRow(8888.0f, 88.0f);
        _drawGearBox(8, false, fx);
        _drawCenteredText("SELF CHECK", 0, Lay::BOT_Y, Lay::W, Lay::BOT_H,
                          &lgfx::fonts::Font0, Col::INK);

    } else {
        _renderLive(s, fx, RunStats{});
        if (Anim::pulse(t, 220)) {
            _spr.fillRoundRect(50, 28, 60, 22, Lay::BOX_R, Col::BOX_BG);
            _spr.drawRoundRect(50, 28, 60, 22, Lay::BOX_R, Col::INK);
            _drawCenteredText("READY", 50, 28, 60, 22,
                              &lgfx::fonts::Font2, Col::INK);
        }
    }
}

void SceneDashboard::_renderLive(const VehicleState& s, const UiFx& fx,
                                 const RunStats& stats) {
    bool live = fx.phase == Phase::LIVE;
    bool flash_on = fx.redline && Anim::pulse(fx.now_ms, SHIFT_FLASH_MS);

    _drawFrame();
    float rpm_bar = _rpmF.update(s.rpm, fx.now_ms,
                                 RPM_ATTACK_MS, RPM_RELEASE_MS);
    float spd = _speedF.update(s.speed_mph, fx.now_ms,
                               SPEED_SMOOTH_MS, SPEED_SMOOTH_MS);
    _drawRPMBar(rpm_bar, flash_on);
    _drawSwoosh();
    _drawHeroRow(s.rpm, spd);           // raw rpm: jitter keeps it alive
    _drawGearBox(s.gear, s.drive_auto, fx);
    _drawRows(s, stats);

    _drawAlertBanner(fx);

    // Redline: frame border flash red/white
    if (fx.redline) {
        uint16_t bc = flash_on ? Col::ACCENT : Col::WHITE;
        _spr.drawRoundRect(Lay::FRAME_X, Lay::FRAME_Y,
                           Lay::FRAME_W, Lay::FRAME_H, Lay::FRAME_R, bc);
        _spr.drawRoundRect(Lay::FRAME_X + 1, Lay::FRAME_Y + 1,
                           Lay::FRAME_W - 2, Lay::FRAME_H - 2,
                           Lay::FRAME_R - 1, bc);
    }

    // BOOT→LIVE reveal: horizontal wipe, coordinated with the navigator
    if (live && fx.phase_ms < LIVE_WIPE_MS) {
        int x = (int)(Lay::W * Ease::inOutQuad(
                    Anim::progress(fx.phase_ms, 0, LIVE_WIPE_MS)));
        if (x < Lay::W) _spr.fillRect(x, 0, Lay::W - x, Lay::H, Col::SKY);
    }
}

void SceneDashboard::_renderFinish(const VehicleState& s, const UiFx& fx,
                                   const RunStats& stats) {
    _drawFrame();

    // Instruments power down in sequence before the result screen
    if (fx.phase_ms < FINISH_SHUTDOWN_MS) {
        float p = (float)fx.phase_ms / FINISH_SHUTDOWN_MS;
        if (p < 0.25f) _drawRows(s, stats);
        if (p < 0.45f) { _drawRPMBar(_rpmF.value, false); _drawSwoosh(); }
        if (p < 0.70f) _drawGearBox(s.gear, s.drive_auto, fx);
        if (p < 0.85f) _drawHeroRow(s.rpm, _speedF.value);
        return;
    }

    _drawCheckerStrip(0, 8, fx.now_ms);
    _drawCheckerStrip(Lay::H - 8, 8, fx.now_ms);

    _drawCenteredText("FINISH", 0, 12, Lay::W, 26,
                      &lgfx::fonts::Font4, Col::INK);

    char buf[10];
    Fmt::speed(buf, stats.max_speed_mph, USE_KMH);
    _drawMetricBox(6, 46, 70, 22, buf, USE_KMH ? "KMH" : "MPH");
    Fmt::rpm(buf, stats.max_rpm);
    _drawMetricBox(84, 46, 70, 22, buf, "RPM");
}

// ── Dispatcher ───────────────────────────────────────────────────────────────

void SceneDashboard::render(const VehicleState& s, const UiFx& fx,
                            const RunStats& stats) {
    switch (fx.phase) {
        case Phase::BOOT:   _renderBoot(s, fx);               break;
        case Phase::FINISH: _renderFinish(s, fx, stats);      break;
        case Phase::LIVE:   _renderLive(s, fx, stats);        break;
    }
}

#endif // NATIVE_BUILD
