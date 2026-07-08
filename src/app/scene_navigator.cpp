#ifndef NATIVE_BUILD
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "scene_navigator.h"
#include "core/formatter.h"
#include "config.h"

// ── Palette (RGB565) — WAY MAP style ─────────────────────────────────────────
static constexpr uint16_t C_BG     = 0x0000; // black chrome
static constexpr uint16_t C_WHITE  = 0xFFFF;
static constexpr uint16_t C_ROW    = 0xFFFF; // roadbook row background
static constexpr uint16_t C_BLACK  = 0x0000; // ink on white rows
static constexpr uint16_t C_YELLOW = 0xFFE0; // active waypoint
static constexpr uint16_t C_RED    = 0xF800; // danger waypoint
static constexpr uint16_t C_GREENB = 0x07E0; // bright green (GPS dot, 4WD)
static constexpr uint16_t C_GREENT = 0x0480; // dark green text on white
static constexpr uint16_t C_BLUE   = 0x0339; // water note text
static constexpr uint16_t C_GRAY   = 0x39E7; // separators
static constexpr uint16_t C_LGRAY  = 0x8410; // dim labels

// ── Layout (240 × 240) ───────────────────────────────────────────────────────
static constexpr int W = 240, H = 240;

static constexpr int HDR_H     = 22;   // header strip
static constexpr int THDR_Y    = 24;   // table column-header row
static constexpr int THDR_H    = 14;
static constexpr int BODY_Y    = 38;   // first roadbook row
static constexpr int ROW_H     = 42;   // row pitch (41 px cell + 1 px gap)
static constexpr int N_ROWS    = 4;    // visible rows
static constexpr int FOOT_Y    = 206;  // footer top

static constexpr int TBL_W     = 172;  // roadbook table width
// Table columns: DIST | ARROW | INFO | TOT
static constexpr int COL_ARR_X  = 46;
static constexpr int COL_INFO_X = 78;
static constexpr int COL_TOT_X  = 138;
static constexpr int CX_DIST    = 23;   // cell centers
static constexpr int CX_ARR     = 62;
static constexpr int CX_INFO    = 108;
static constexpr int CX_TOT     = 155;

static constexpr int SIDE_X    = 172;  // right column separator
static constexpr int SIDE_CX   = 206;  // right column center

SceneNavigator::SceneNavigator(lgfx::LGFX_Device& disp, const Roadbook& rb)
    : _spr(&disp), _disp(disp), _rb(rb) {}

void SceneNavigator::init() {
    _spr.setColorDepth(16);
    _spr.setPsram(false);          // internal RAM → DMA-friendly (112.5 KB)
    _spr.createSprite(W, H);
}

void SceneNavigator::push() {
    _spr.pushSprite(&_disp, 0, 0);
}

// ── Drawing helpers ──────────────────────────────────────────────────────────

void SceneNavigator::_thickLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    _spr.drawLine(x0, y0, x1, y1, color);
    if (abs(dx) >= abs(dy)) {   // mostly horizontal → widen vertically
        _spr.drawLine(x0, y0 - 1, x1, y1 - 1, color);
        _spr.drawLine(x0, y0 + 1, x1, y1 + 1, color);
    } else {                    // mostly vertical → widen horizontally
        _spr.drawLine(x0 - 1, y0, x1 - 1, y1, color);
        _spr.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    }
}

// Arc from a0 to a1 (degrees, math convention: 0=east, 90=north)
void SceneNavigator::_arcLines(int cx, int cy, int r, int a0_deg, int a1_deg,
                               uint16_t color) {
    const float d2r = 0.017453f;
    int steps = abs(a1_deg - a0_deg) / 10;
    if (steps < 2) steps = 2;
    float px = cx + r * cosf(a0_deg * d2r);
    float py = cy - r * sinf(a0_deg * d2r);
    for (int i = 1; i <= steps; i++) {
        float a = (a0_deg + (a1_deg - a0_deg) * i / (float)steps) * d2r;
        float nx = cx + r * cosf(a);
        float ny = cy - r * sinf(a);
        _spr.drawLine((int)px, (int)py, (int)nx, (int)ny, color);
        px = nx; py = ny;
    }
}

void SceneNavigator::_thickArc(int cx, int cy, int r, int a0_deg, int a1_deg,
                               uint16_t color) {
    _arcLines(cx, cy, r - 1, a0_deg, a1_deg, color);
    _arcLines(cx, cy, r,     a0_deg, a1_deg, color);
    _arcLines(cx, cy, r + 1, a0_deg, a1_deg, color);
}

// ── Direction arrows (pure primitives, drawn on white cell) ─────────────────

void SceneNavigator::_drawArrow(int cx, int cy, const Waypoint& wp) {
    const uint16_t ink = C_BLACK;
    uint16_t dot = wp.danger              ? C_RED
                 : wp.kind == NoteKind::INFO_BLUE ? C_BLUE
                 : wp.kind == NoteKind::GOOD      ? C_GREENT
                 :                                  C_BLACK;

    switch (wp.dir) {

    case WaypointDir::STRAIGHT:
        _thickLine(cx, cy + 10, cx, cy - 4, ink);
        _spr.fillTriangle(cx, cy - 12, cx - 5, cy - 3, cx + 5, cy - 3, ink);
        _spr.fillCircle(cx, cy + 12, 3, dot);
        break;

    case WaypointDir::RIGHT_45:
        _thickLine(cx - 3, cy + 10, cx - 3, cy + 2, ink);
        _thickLine(cx - 3, cy + 2,  cx + 4, cy - 5, ink);
        _spr.fillTriangle(cx + 9, cy - 10, cx + 1, cy - 8, cx + 7, cy - 2, ink);
        _spr.fillCircle(cx - 3, cy + 12, 3, dot);
        break;

    case WaypointDir::LEFT_45:
        _thickLine(cx + 3, cy + 10, cx + 3, cy + 2, ink);
        _thickLine(cx + 3, cy + 2,  cx - 4, cy - 5, ink);
        _spr.fillTriangle(cx - 9, cy - 10, cx - 1, cy - 8, cx - 7, cy - 2, ink);
        _spr.fillCircle(cx + 3, cy + 12, 3, dot);
        break;

    case WaypointDir::RIGHT_90:
        _thickLine(cx - 6, cy + 10, cx - 6, cy + 2, ink);
        _thickArc(cx - 1, cy + 2, 5, 90, 180, ink);   // corner
        _thickLine(cx - 1, cy - 3, cx + 3, cy - 3, ink);
        _spr.fillTriangle(cx + 10, cy - 3, cx + 2, cy - 8, cx + 2, cy + 2, ink);
        _spr.fillCircle(cx - 6, cy + 12, 3, dot);
        break;

    case WaypointDir::LEFT_90:
        _thickLine(cx + 6, cy + 10, cx + 6, cy + 2, ink);
        _thickArc(cx + 1, cy + 2, 5, 0, 90, ink);
        _thickLine(cx + 1, cy - 3, cx - 3, cy - 3, ink);
        _spr.fillTriangle(cx - 10, cy - 3, cx - 2, cy - 8, cx - 2, cy + 2, ink);
        _spr.fillCircle(cx + 6, cy + 12, 3, dot);
        break;

    case WaypointDir::HAIRPIN_L:
        _thickLine(cx + 5, cy + 10, cx + 5, cy - 2, ink);   // up
        _thickArc(cx, cy - 2, 5, 0, 180, ink);              // U-turn
        _thickLine(cx - 5, cy - 2, cx - 5, cy + 2, ink);    // down
        _spr.fillTriangle(cx - 5, cy + 9, cx - 9, cy + 1, cx - 1, cy + 1, ink);
        _spr.fillCircle(cx + 5, cy + 12, 3, dot);
        break;

    case WaypointDir::HAIRPIN_R:
        _thickLine(cx - 5, cy + 10, cx - 5, cy - 2, ink);
        _thickArc(cx, cy - 2, 5, 0, 180, ink);
        _thickLine(cx + 5, cy - 2, cx + 5, cy + 2, ink);
        _spr.fillTriangle(cx + 5, cy + 9, cx + 1, cy + 1, cx + 9, cy + 1, ink);
        _spr.fillCircle(cx - 5, cy + 12, 3, dot);
        break;

    case WaypointDir::WATER:
        // hatched water band
        for (int x = cx - 14; x <= cx + 12; x += 5) {
            _spr.drawFastHLine(x, cy + 5, 3, C_BLUE);
            _spr.drawFastHLine(x + 2, cy + 8, 3, C_BLUE);
        }
        _thickLine(cx, cy + 11, cx, cy - 4, ink);
        _spr.fillTriangle(cx, cy - 12, cx - 5, cy - 3, cx + 5, cy - 3, ink);
        _spr.fillCircle(cx + 11, cy + 9, 3, C_BLUE);
        break;

    case WaypointDir::FINISH: {
        _thickLine(cx - 4, cy + 10, cx - 4, cy - 4, ink);
        _spr.fillTriangle(cx - 4, cy - 12, cx - 9, cy - 3, cx + 1, cy - 3, ink);
        // checkered mini-flag
        const int fx = cx + 3, fy = cy - 11, c = 3;
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++)
                if ((i + j) % 2 == 0)
                    _spr.fillRect(fx + i * c, fy + j * c, c, c, ink);
        _spr.drawRect(fx, fy, 4 * c, 4 * c, ink);
        _spr.fillCircle(cx - 4, cy + 12, 3, dot);
        break;
    }
    }
}

// ── Header ───────────────────────────────────────────────────────────────────

void SceneNavigator::_drawHeader(const VehicleState& s) {
    uint32_t ms = millis();

    _spr.fillRect(0, 0, W, HDR_H, C_BG);
    _spr.drawFastHLine(0, HDR_H, W, C_GRAY);

    // GPS status + simulated sat count
    _spr.fillCircle(8, 11, 3, C_GREENB);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);
    _spr.setTextColor(C_GREENB, C_BG);
    _spr.setTextDatum(lgfx::TL_DATUM);
    _spr.drawString("GPS", 15, 7);
    char sat[10];
    snprintf(sat, sizeof(sat), "SAT %d", 10 + (int)((ms / 7000) % 3));
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.drawString(sat, 40, 7);

    // Stage title
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextDatum(lgfx::TC_DATUM);
    _spr.drawString("WAY MAP", W / 2, 3);

    // Clock (simulated, starts at 12:00:00)
    char clk[10];
    Fmt::clockHMS(clk, ms / 1000);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextDatum(lgfx::TR_DATUM);
    _spr.drawString(clk, W - 4, 7);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Roadbook table ───────────────────────────────────────────────────────────

void SceneNavigator::_drawRow(int y, const Waypoint& wp, bool active) {
    const int h = ROW_H - 1;

    // Row background + DIST cell highlight (like the WAY MAP reference)
    _spr.fillRect(0, y, TBL_W, h, C_ROW);
    uint16_t dist_bg  = active    ? C_YELLOW
                      : wp.danger ? C_RED
                      :             C_ROW;
    uint16_t dist_ink = wp.danger && !active ? C_WHITE : C_BLACK;
    if (dist_bg != C_ROW) _spr.fillRect(0, y, COL_ARR_X, h, dist_bg);

    // Column separators
    _spr.drawFastVLine(COL_ARR_X,  y, h, C_BLACK);
    _spr.drawFastVLine(COL_INFO_X, y, h, C_BLACK);
    _spr.drawFastVLine(COL_TOT_X,  y, h, C_BLACK);

    const int cy = y + h / 2;

    // DIST — segment distance, big
    char buf[10];
    Fmt::distNum(buf, wp.dist_mi, USE_KMH);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextSize(1);
    _spr.setTextDatum(lgfx::MC_DATUM);
    _spr.setTextColor(dist_ink, dist_bg);
    _spr.drawString(buf, CX_DIST, cy);

    // Direction arrow
    _drawArrow(CX_ARR, y + 19, wp);

    // INFO — main line (+ optional colored note line)
    uint16_t note_col = wp.kind == NoteKind::WARN      ? C_RED
                      : wp.kind == NoteKind::GOOD      ? C_GREENT
                      : wp.kind == NoteKind::INFO_BLUE ? C_BLUE
                      :                                  C_BLACK;
    bool has_note = wp.note && wp.note[0];
    bool short_info = strlen(wp.info) <= 7;
    _spr.setFont(short_info ? (const lgfx::IFont*)&lgfx::fonts::Font2
                            : (const lgfx::IFont*)&lgfx::fonts::Font0);
    _spr.setTextColor(C_BLACK, C_ROW);
    _spr.drawString(wp.info, CX_INFO, has_note ? y + 13 : cy);
    if (has_note) {
        _spr.setFont(&lgfx::fonts::Font0);
        _spr.setTextColor(note_col, C_ROW);
        _spr.drawString(wp.note, CX_INFO, y + 29);
    }

    // TOTAL — cumulative distance
    Fmt::distNum(buf, wp.total_mi, USE_KMH);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_BLACK, C_ROW);
    _spr.drawString(buf, CX_TOT, cy);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

void SceneNavigator::_drawTable(const VehicleState& s) {
    // Column headers
    _spr.fillRect(0, THDR_Y, TBL_W, THDR_H, 0x2104);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);
    _spr.setTextColor(C_WHITE, 0x2104);
    _spr.setTextDatum(lgfx::TC_DATUM);
    const char* dunit = USE_KMH ? "KM" : "MI";
    char hd[10];
    snprintf(hd, sizeof(hd), "DIST %s", dunit);
    _spr.drawString(hd,     CX_DIST, THDR_Y + 3);
    _spr.drawString("DIR",  CX_ARR,  THDR_Y + 3);
    _spr.drawString("INFO", CX_INFO, THDR_Y + 3);
    _spr.drawString("TOT",  CX_TOT,  THDR_Y + 3);
    _spr.setTextDatum(lgfx::TL_DATUM);

    // Scrolling window: keep active row second from top when possible
    int first = s.active_wp - 1;
    int max_first = _rb.getCount() - N_ROWS;
    if (first > max_first) first = max_first;
    if (first < 0) first = 0;

    for (int i = 0; i < N_ROWS && (first + i) < _rb.getCount(); i++) {
        _drawRow(BODY_Y + i * ROW_H, _rb.get(first + i),
                 (first + i) == s.active_wp);
    }
}

// ── Right column: SPEED / GEAR / RPM / 4WD ──────────────────────────────────

void SceneNavigator::_drawSideColumn(const VehicleState& s) {
    _spr.fillRect(SIDE_X, HDR_H + 1, 2, FOOT_Y - HDR_H - 1, C_GRAY);

    char buf[10];
    _spr.setTextDatum(lgfx::TC_DATUM);

    // SPEED
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextSize(1);
    _spr.setTextColor(C_LGRAY, C_BG);
    _spr.drawString("SPEED", SIDE_CX, 27);
    Fmt::speed(buf, s.speed_mph, USE_KMH);
    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(1.5f);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.drawString(buf, SIDE_CX, 37);
    _spr.setTextSize(1);
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LGRAY, C_BG);
    _spr.drawString(USE_KMH ? "km/h" : "MPH", SIDE_CX, 79);

    _spr.drawFastHLine(SIDE_X + 4, 92, W - SIDE_X - 8, C_GRAY);

    // GEAR
    _spr.drawString("GEAR", SIDE_CX, 96);
    Fmt::gear(buf, s.gear, false);   // navigator shows the actual gear number
    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(1.5f);
    _spr.setTextColor(C_YELLOW, C_BG);
    _spr.drawString(buf, SIDE_CX, 106);
    _spr.setTextSize(1);

    _spr.drawFastHLine(SIDE_X + 4, 152, W - SIDE_X - 8, C_GRAY);

    // RPM
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(C_LGRAY, C_BG);
    _spr.drawString("RPM", SIDE_CX, 156);
    Fmt::rpm(buf, s.rpm);
    _spr.setFont(&lgfx::fonts::Font2);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.drawString(buf, SIDE_CX, 166);

    // 4WD status
    _spr.setFont(&lgfx::fonts::Font0);
    _spr.setTextColor(s.four_wd_lock ? C_GREENB : C_LGRAY, C_BG);
    _spr.drawString(s.four_wd_lock ? "4WD LOCK" : "4WD AUTO", SIDE_CX, 193);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Footer: heading + distance to next waypoint ──────────────────────────────

void SceneNavigator::_drawFooter(const VehicleState& s) {
    _spr.fillRect(0, FOOT_Y, W, H - FOOT_Y, C_BG);
    _spr.drawFastHLine(0, FOOT_Y, W, C_YELLOW);
    _spr.drawFastHLine(0, FOOT_Y + 1, W, C_YELLOW);

    char buf[12];
    Fmt::headingFull(buf, s.heading_deg);
    _spr.setFont(&lgfx::fonts::Font4);
    _spr.setTextSize(1);
    _spr.setTextColor(C_WHITE, C_BG);
    _spr.setTextDatum(lgfx::TL_DATUM);
    _spr.drawString(buf, 4, 211);

    char d[10];
    Fmt::distNum(d, s.dist_to_next, USE_KMH);
    snprintf(buf, sizeof(buf), "%s %s", d, USE_KMH ? "KM" : "MI");
    _spr.setTextColor(C_YELLOW, C_BG);
    _spr.setTextDatum(lgfx::TR_DATUM);
    _spr.drawString(buf, W - 4, 211);
    _spr.setTextDatum(lgfx::TL_DATUM);
}

// ── Main render ──────────────────────────────────────────────────────────────

void SceneNavigator::render(const VehicleState& s) {
    _spr.fillScreen(C_BG);
    _drawHeader(s);
    _drawTable(s);
    _drawSideColumn(s);
    _drawFooter(s);
}

#endif // NATIVE_BUILD
