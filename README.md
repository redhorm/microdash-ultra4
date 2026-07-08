# ESP32-S3 Dual Display — RC Ultra4 Dashboard + Waypoint Navigator

Due display SPI indipendenti pilotati in parallelo (DMA su due host SPI separati):

| Display | Pannello | Risoluzione | Ruolo | Host SPI | Target FPS |
|---|---|---|---|---|---|
| A | ST7735S 0.96" | 160×80 | **Dashboard** stile cluster Jeep off-road | `SPI2_HOST` (FSPI) | 30+ |
| B | ST7789 1.3" | 240×240 | **Waypoint Navigator** stile roadbook Ultra4 "WAY MAP" | `SPI3_HOST` (HSPI) | ~12 |

Un simulatore physics-based (C++ puro, testabile su host) genera velocità, RPM, marce,
batteria, temperatura e un odometro che fa avanzare i waypoint del roadbook demo (9 WP, ~12 mi, in loop).


## Pinout (ESP32-S3 DevKitC-1)

Tutto il pinout e il tuning vivono in **`include/config.h`** — unico punto di modifica.

### Display A — ST7735S 160×80 (Dashboard)

| Segnale modulo | GPIO | Note |
|---|---|---|
| SDA / MOSI | **11** | FSPI MOSI |
| SCL / SCLK | **12** | FSPI SCLK |
| CS         | **10** | |
| DC / A0    | **9**  | |
| RST        | **8**  | `-1` in config.h se legato a 3.3 V |
| BLK / BL   | **46** | PWM; `-1` se legato a 3.3 V |
| VCC        | 3.3 V  | |
| GND        | GND    | |

### Display B — ST7789 240×240 (Navigator)

| Segnale modulo | GPIO | Note |
|---|---|---|
| SDA / MOSI | **13** | HSPI MOSI |
| SCL / SCLK | **14** | HSPI SCLK |
| CS         | **15** | molti moduli 240×240 non hanno CS: lascia il pin libero e metti `-1` |
| DC         | **16** | |
| RST        | **17** | |
| BLK / BL   | **18** | |
| VCC        | 3.3 V  | |
| GND        | GND    | |

Cavi corti (<10 cm) per i clock a 40/80 MHz; se vedi glitch abbassa
`DISP_A_SPI_HZ` / `DISP_B_SPI_HZ` in config.h.

## Tuning ST7735 160×80 (leggi prima di dare la colpa al wiring!)

I moduli 0.96" 160×80 usano una finestra 80×160 dentro una GRAM 132×162 e
richiedono quasi sempre offset e spesso inversione colore. In `config.h`:

| Sintomo | Rimedio |
|---|---|
| Immagine spostata / bordo di pixel random | ritocca `DISP_A_COL_OFFSET` (tipico **26**, a volte 24) e `DISP_A_ROW_OFFSET` (tipico **1**, a volte 0) |
| Colori invertiti (sfondo bianco invece che nero) | inverti `DISP_A_INVERT` |
| Rosso e blu scambiati | inverti `DISP_A_RGB_ORDER` |
| Immagine capovolta | `DISP_A_ROTATION` 1 ↔ 3 (landscape), `DISP_B_ROTATION` 0 ↔ 2 |

Per l'ST7789: quasi tutti i moduli 240×240 vogliono `DISP_B_INVERT = true` (già il default).

## Build / Flash / Monitor

```bash
pio run                          # build firmware
pio run -t upload                # flash (USB-C nativo, auto-reset 1200bps)
pio device monitor               # monitor 115200 (stampa FPS e stato sim ogni 1s)
pio test -e native               # unit test host-compiled (g++, nessun hardware)
```

Senza PlatformIO i test girano anche con g++ puro:

```bash
g++ -std=c++17 -DNATIVE_BUILD -I src -I include -I <unity> \
    src/core/*.cpp test/test_core/test_main.cpp -o test_core && ./test_core
```

## Architettura

```
include/config.h        ← pin, offset, velocità SPI, unità (MPH/km/h, °F/°C)
src/
  core/                 ← C++ puro, ZERO dipendenze Arduino (testabile su host)
    state.h             ← VehicleState condiviso tra i due display
    simulator.{h,cpp}   ← fisica: throttle/brake → velocità/RPM/marce/batteria/temp
    roadbook.{h,cpp}    ← stage demo 9 waypoint + avanzamento su odometro
    formatter.{h,cpp}   ← formattazione valori in buffer (no heap)
  hal/
    display_a.h         ← LGFX device ST7735S su SPI2_HOST + DMA
    display_b.h         ← LGFX device ST7789  su SPI3_HOST + DMA
  app/
    scene_dashboard.*   ← render 160×80 (sprite full-frame)
    scene_navigator.*   ← render 240×240 (sprite full-frame)
  main.cpp              ← loop core 1 = sim + dashboard 30 FPS
                          task core 0 = navigator 12 FPS
test/test_core/         ← unit test Unity (roadbook, simulatore, formatter)
```

- **Frame rate disaccoppiati**: `DASH_FRAME_MS` (33 ms) e `NAV_FRAME_MS` (80 ms) in config.h.
- **Niente delay()**: il loop si autoregola con `millis()`, il task navigator con `vTaskDelayUntil()`.
- **Due host SPI + DMA**: le push dei due sprite avvengono in parallelo su bus fisicamente separati; gli sprite sono forzati in RAM interna (`setPsram(false)`) per restare DMA-friendly.

### Pitfall noto (già gestito): `build_src_filter`

In `platformio.ini` il filtro elenca esplicitamente `+<core/*.cpp>` ecc.:
con il solo `+<*>` alcune versioni di PIO non linkano i .cpp nelle sottocartelle
**silenziosamente** (il firmware compila ma i simboli mancano o usano stub).
Se aggiungi una cartella sotto `src/`, aggiungila anche al filtro.

## UI

**Dashboard 160×80** — sfondo nero, accent lime: velocità grande al centro (7-seg),
marcia D/1-5 in giallo con modalità AUTO/MAN, barra RPM a segmenti verde→giallo→rosso,
angoli alti con batteria e temperatura + micro-barre, riga inferiore heading + 4WD LOCK.

**Navigator 240×240** — header con GPS/SAT, titolo WAY MAP e orologio; tabella roadbook
a righe bianche (DIST | freccia | INFO | TOT) con waypoint attivo evidenziato in giallo e
pericoli in rosso, frecce disegnate con primitive (dritto, 45°, 90°, hairpin, guado, finish);
colonna destra con SPEED / GEAR / RPM / 4WD; footer con heading e distanza al prossimo
waypoint. La lista scorre da sola man mano che l'odometro simulato avanza.

## Unità

`config.h`: `USE_KMH` (default `false` → MPH/mi come le reference) e `USE_CELSIUS`
(default `false` → °F). Cambiale e tutta la UI si adegua.
