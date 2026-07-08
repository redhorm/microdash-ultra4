// ============================================================
// ESP32-S3 Dual Display — RC Ultra4 Dashboard + Waypoint Navigator
//
// Core 1 (Arduino loop): simulator + dashboard @ ~30 FPS (ST7735, SPI2)
// Core 0 (FreeRTOS task): navigator @ ~12 FPS (ST7789, SPI3)
// Separate SPI hosts → the two DMA pushes run truly in parallel.
// No blocking delay(): loop() paces itself with millis(), the
// navigator task with vTaskDelayUntil().
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "hal/display_a.h"
#include "hal/display_b.h"
#include "core/simulator.h"
#include "core/roadbook.h"
#include "app/scene_dashboard.h"
#include "app/scene_navigator.h"

static LGFX_DisplayA   dispA;
static LGFX_DisplayB   dispB;
static Simulator       sim;
static Roadbook        roadbook;
static SceneDashboard  dash(dispA);
static SceneNavigator  nav(dispB, roadbook);

// Snapshot shared with the navigator task (guarded by a spinlock;
// the struct is small, the copy takes ~a microsecond).
static VehicleState  snapshot;
static portMUX_TYPE  snapMux = portMUX_INITIALIZER_UNLOCKED;

static void navTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        VehicleState s;
        taskENTER_CRITICAL(&snapMux);
        s = snapshot;
        taskEXIT_CRITICAL(&snapMux);

        nav.render(s);
        nav.push();

        vTaskDelayUntil(&last, pdMS_TO_TICKS(NAV_FRAME_MS));
    }
}

void setup() {
    Serial.begin(115200);

    dispA.init();
    dispB.init();
    if (DISP_A_BL >= 0) dispA.setBrightness(220);
    if (DISP_B_BL >= 0) dispB.setBrightness(220);

    dash.init();
    nav.init();
    sim.reset();
    roadbook.reset();

    xTaskCreatePinnedToCore(navTask, "nav", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
    static uint32_t last_frame = 0;
    static uint32_t last_sim   = 0;
    static uint32_t fps_frames = 0, fps_t0 = 0;

    uint32_t now = millis();
    if (now - last_frame < DASH_FRAME_MS) return;
    last_frame = now;

    // Simulation step (dt clamped inside Simulator::update)
    sim.update(now - last_sim);
    last_sim = now;
    roadbook.update(sim.state);

    taskENTER_CRITICAL(&snapMux);
    snapshot = sim.state;
    taskEXIT_CRITICAL(&snapMux);

    dash.render(sim.state);
    dash.push();

    // FPS report once per second
    fps_frames++;
    if (now - fps_t0 >= 1000) {
        Serial.printf("[dash] %u FPS  spd=%.0f rpm=%.0f wp=%d odo=%.2f\n",
                      (unsigned)fps_frames, sim.state.speed_mph,
                      sim.state.rpm, sim.state.active_wp, sim.state.odo_mi);
        fps_frames = 0;
        fps_t0     = now;
    }
}
