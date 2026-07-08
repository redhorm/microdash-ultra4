#pragma once
// ================================================================
// config.h  — single point of modification for all hardware tuning
// ================================================================

// ----------------------------------------------------------------
// PIN TABLE
// ----------------------------------------------------------------
//  Signal          GPIO   Notes
//  ──────────────  ─────  ─────────────────────────────────────────
// DISPLAY A  ST7735S  160×80   SPI2_HOST (FSPI)
#define DISP_A_MOSI   11   // FSPI MOSI
#define DISP_A_SCLK   12   // FSPI SCLK
#define DISP_A_CS     10   // Chip Select
#define DISP_A_DC      9   // Data / Command
#define DISP_A_RST     8   // Reset  (or -1 to omit)
#define DISP_A_BL     46   // Backlight PWM; set -1 to tie BL → 3.3 V

// DISPLAY B  ST7789   240×240  SPI3_HOST (HSPI)
#define DISP_B_MOSI   13   // HSPI MOSI
#define DISP_B_SCLK   14   // HSPI SCLK
#define DISP_B_CS     15   // Chip Select
#define DISP_B_DC     16   // Data / Command
#define DISP_B_RST    17   // Reset  (or -1)
#define DISP_B_BL     18   // Backlight; set -1 to tie BL → 3.3 V

// ----------------------------------------------------------------
// ST7735  160×80  offset / inversion tuning
// Most 160×80 IPS blue-PCB modules: col=26 row=1 invert=false BGR
// If image is wrong color → flip DISP_A_INVERT
// If image is shifted     → adjust COL/ROW offsets
// ----------------------------------------------------------------
#define DISP_A_COL_OFFSET   26
#define DISP_A_ROW_OFFSET    1
#define DISP_A_INVERT     false   // try true if colors are inverted
#define DISP_A_RGB_ORDER  false   // false = BGR (default for ST7735)
#define DISP_A_ROTATION      1    // 1/3 = landscape 160×80 (module native is portrait 80×160)

// ----------------------------------------------------------------
// ST7789  240×240
// ----------------------------------------------------------------
#define DISP_B_INVERT      true   // almost all ST7789 modules need this
#define DISP_B_RGB_ORDER  false
#define DISP_B_ROTATION      0    // 0-3; adjust if display is upside-down

// ----------------------------------------------------------------
// SPI clock speeds
// ----------------------------------------------------------------
#define DISP_A_SPI_HZ   40000000UL   // 40 MHz  (ST7735S max ~60 MHz)
#define DISP_B_SPI_HZ   80000000UL   // 80 MHz  (ST7789 supports 80 MHz)

// ----------------------------------------------------------------
// Frame-rate targets
// ----------------------------------------------------------------
#define DASH_FRAME_MS   33    // ~30 FPS  dashboard
#define NAV_FRAME_MS    80    // ~12 FPS  navigator

// ----------------------------------------------------------------
// Unit preferences
// ----------------------------------------------------------------
#define USE_KMH     false   // false = MPH, true = km/h
#define USE_CELSIUS false   // false = °F,  true = °C

// ----------------------------------------------------------------
// Simulator limits
// ----------------------------------------------------------------
#define SIM_MAX_SPEED_MPH  110.0f
#define SIM_MAX_RPM       7200.0f
#define SIM_IDLE_RPM       800.0f
#define SIM_NUM_GEARS        5

