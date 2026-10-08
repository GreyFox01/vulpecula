/* ===========================================================================
   Vulpecula.ino
   ---------------------------------------------------------------------------
   Proximity-gated dual-band WiFi + BLE recording-device detector
   for the RockBase IoT NM-CYD-C5  (ESP32-C5-WROOM-1, 2.8" 320x240 touch).

   Receive only: no probe requests, no active BLE
   scanning, no association, nothing transmitted.

   ---------------------------------------------------------------------------
   WHAT THIS IS FOR

   Sweeping a space you occupy or are authorised to assess - a hotel room, a
   rental, an office - for wireless cameras and microphones.

   The ~2 m gate is a SPATIAL FILTER, not a sensitivity limit. Room coverage
   comes from you walking the board around. At full sensitivity a hotel gives
   you 100-300 BLE devices and 50+ access points; the gate cuts that to single
   digits. That is the difference between an instrument and a log file.

   ---------------------------------------------------------------------------
   LAYOUT

   The firmware lives in Vulpecula/src/. Arduino compiles a sketch's src/
   folder recursively, and - the point of the move - it does NOT run the
   prototype-injection preprocessor on files there. That preprocessor
   generates a forward declaration for every top-level function in a .ino and
   injects the set at the first function definition it finds, so a prototype
   naming a later-declared type fails to parse and the errors cascade into
   dozens of misleading messages. Three separate builds were broken that way.
   Code in src/ is ordinary C++ with ordinary headers.

   src/pure.h       the hardware-free core: gate, triage, classifier, and all
                    input parsing. Compiles on a host with no Arduino headers
                    at all, which is why the tests include the real
                    implementation rather than a copy sliced out of a sketch.
   src/vulpecula.h  pure.h plus everything that touches the board.

   This file holds setup(), loop() and the serial console, and nothing else.

   ARDUINO IDE SETUP   (IDE 2.x)

   1. Boards Manager -> "esp32" by Espressif, version 3.3.5 or newer.
      Older cores have no ESP32-C5 support and will not compile.

   2. Library Manager -> "NimBLE-Arduino" by h2zero, version 2.2.x or newer.
      That is the ONLY library you need. The display and touch drivers are in
      this file, so there is no TFT_eSPI to install, configure or patch.

   3. Tools menu:
        Board .................. ESP32C5 Dev Module
        USB CDC On Boot ........ Enabled
        Flash Size ............. 16MB (128Mb)
        Partition Scheme ....... Huge APP (3MB No OTA/1MB SPIFFS)
        PSRAM .................. Enabled          <-- required, table lives there
        Upload Speed ........... 460800
        Core Debug Level ....... None

   4. Plug into the ESP32-C5 USB-C port (the one that is NOT the CH340),
      select the port, Upload.

   5. First boot runs a display self-test: colour bars labelled R / G / B.
      If red and blue are swapped, set PANEL_BGR to 0 below and reflash.
      If the screen stays dark, see "if the display does not work".

   ---------------------------------------------------------------------------
   WHY ARDUINO IDE IS FINE HERE, AND WHEN IT IS NOT

   Normally PlatformIO wins for this board, for one reason: TFT_eSPI needs its
   configuration passed as compile-time flags and needs the vendor's
   TFT_eSPI_ESP32_C5.c/h files dropped into the library, because upstream
   TFT_eSPI has no C5 backend. In Arduino IDE that means hand-editing a
   managed library, which gets wiped on every library update.

   This file sidesteps that entirely by talking to the ST7789 over the stock
   Arduino SPI library in about 200 lines. No library patching, so Arduino IDE
   becomes the simpler choice.

   Use PlatformIO instead if you want pinned dependency versions, a custom
   partition table in the repo, or to split this back into modules.

   ---------------------------------------------------------------------------
   PIN PROVENANCE  -  read this before changing anything

   SCK/MISO/MOSI and the three CS pins are from the vendor's own pinout table
   (README and wiki). Backlight and RGB LED likewise.

   TFT_DC = 24 is NOT in the vendor pinout table. It comes from RockBase-iot
   /NM-CYD-C5 issue #3, filed by someone who brought the board up on
   TFT_eSPI and published a working config. Same source for TFT_RST = -1
   (no reset line; we issue a software reset instead) and for SPI at 20 MHz
   rather than 40.

   That issue also documents two colour traps that this file handles:
     - the panel needs INVERSION OFF. TFT_eSPI's ST7789 init table sends
       INVON unconditionally, which is why people see red render as yellow
       or cyan. We send INVOFF (0x20) instead. See PANEL_INVERT.
     - colour order may need to be BGR rather than RGB. See PANEL_BGR.

   The wiki calls the touch panel capacitive while also listing an XPT2046 on
   GPIO 1. XPT2046 is a resistive controller, so the wiki contradicts itself.
   This file drives it as resistive SPI, which matches the vendor README and
   every community project. If your unit is genuinely capacitive, touch will
   not respond and you will need an I2C driver on IO8/IO9 instead - the rest
   of the firmware still works, it is just read-only.

   ---------------------------------------------------------------------------
   IF THE DISPLAY DOES NOT WORK

   Set SERIAL_ONLY to 1. The whole detector runs headless over the serial
   monitor at 115200, so you can verify the radio side before fighting the
   panel. Then try, in order:
     - PANEL_DRIVER 1 (ILI9341). The vendor says some units ship that way.
     - PANEL_BGR 0
     - PANEL_INVERT 1
     - SPI_HZ_LCD 10000000
     - TFT_DC 21, then 22, then 20. Issue #3 is one report, not a datasheet.

   ---------------------------------------------------------------------------
   CALIBRATE BEFORE YOU TRUST A READING

   The thresholds below are path-loss maths plus an assumed implementation
   margin. They are a starting point, not a measurement of YOUR board and
   antenna. SETUP screen -> right half starts the guided routine. Full
   procedure is in CALIBRATION.md alongside this sketch.

   The one measurement that matters most: put a reference transmitter 2 m away
   on the far side of an interior wall and confirm it reads ambient. Rejecting
   the next room is the entire premise.

   ---------------------------------------------------------------------------
   LIMITS  -  a tool that implies "all clear" is worse than no tool

   - A camera recording to SD with its radio off is INVISIBLE here. Largest
     blind spot, unfixable in firmware. Always also sweep for lenses.
   - LTE/4G cameras, wired cameras and analog transmitters are invisible.
   - No detector can tell whether a device is recording.
   - Absence of an alert is never proof of absence.

   Copyright (c) 2026 The Wolf Creek Assembly
   SPDX-License-Identifier: MIT
   Licence: MIT, see LICENSE. Signature tables are leads requiring confirmation,
   not proof of anything. Radio, privacy and surveillance law varies; none of
   this is legal advice.
   =========================================================================== */

#include "src/vulpecula.h"

// ===========================================================================
// TASKS
//
// The radio pump and the UI have very different latency needs: dropping a
// captured frame loses evidence, dropping a frame of UI loses nothing. So the
// pump runs at higher priority on its own task and the UI rides the Arduino
// loop.
// ===========================================================================

static void pump_task(void *arg)
{
    (void)arg;
    uint32_t last_tick = 0, last_log = 0;
    for (;;) {
        uint32_t now = millis();
        sched_tick(now);
        wifi_cap_pump(now);
        ble_cap_pump(now);
        ie_pump(now);          // identity parsing, a few frames per tick

        // 5 Hz. Per-packet would be wasted work; slower and the sweep meter
        // lags the operator's hand.
        if ((uint32_t)(now - last_tick) >= 200) {
            last_tick = now;
            tracks_tick(now);

            if (s_cal == CAL_NEAR || s_cal == CAL_CONTACT) {
                // Lock onto the reference BY NAME. This used to take the
                // top-ranked track on the band, which is whatever scored
                // highest - potentially a neighbour's access point. That
                // produced a plausible threshold measured against entirely
                // the wrong transmitter, with no symptom.
                track_t *ref = cal_find_ref();
                s_cal_ref_seen = (ref != NULL);
                if (ref) {
                    int8_t pk = ring_peak(&ref->ring, now, PEAK_WINDOW_MS);
                    if (pk != INT8_MIN) {
                        s_cal_last_rssi = pk;
                        cal_feed(pk);
                        // Pin to the channel it is actually on, so a 5 GHz
                        // reference is not missed while hopping 25 channels.
                        if (s_cal_band != BAND_BLE && ref->channel)
                            sched_cal_channel(ref->channel);
                    }
                }
            }

            for (uint16_t i = 0; i < MAX_TRACKS; i++) {
                track_t *t = track_at(i);
                if (!t) continue;
                if (t->tier >= TIER_NEAR && !t->alerted && t->score >= SCORE_POSSIBLE) {
                    t->alerted = true;
                    log_snapshot(now, "ALERT");
                    char ps[20]; log_pseudonym(t->mac, ps, sizeof(ps));
                    Serial.printf("[%s] %s %s %s %ddBm %s/%s %.16s %.16s\n",
                                  risk_name(t->risk), tier_name(t->tier),
                                  band_name(t->band), ps, (int)t->peak_session,
                                  dtype_name(t->dtype), dtier_name(t->dtype_tier),
                                  t->vendor, t->name);
                }
            }
        }

        // Periodic snapshot so a crash or a flat battery does not cost the
        // whole sweep.
        if ((uint32_t)(now - last_log) >= 30000) {
            last_log = now;
            log_snapshot(now, "PERIODIC");
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== Vulpecula ===");
    Serial.println("proximity-gated Wi-Fi + BLE recording-device sweep");

    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase(); nvs_flash_init();
    }

    pinMode(PIN_WS2812, OUTPUT);
    led_rgb(0, 0, 30);                          // blue = booting

    spi_mux_init();
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);
    pinMode(PIN_TOUCH_CS, OUTPUT); digitalWrite(PIN_TOUCH_CS, HIGH);   // once, not per read
    pinMode(PIN_SD_CS,    OUTPUT); digitalWrite(PIN_SD_CS,    HIGH);

#if !SERIAL_ONLY
    disp_cfg_load();
    lcd_init();
    fill_screen(C_BLACK);
    // No radio splash here: the 5 GHz probe moved to the first sweep, so
    // nothing radio-related happens during setup() any more.
    draw_text_12(6, 6, "STARTING", C_CYAN, C_BLACK);
#endif

    prox_init();
    touch_cal_load();
    if (!tracks_init()) {
        Serial.println("FATAL: track table alloc failed");
#if !SERIAL_ONLY
        fill_screen(C_RED);
        draw_text_12(6, 100, "NO PSRAM - ENABLE IT", C_WHITE, C_RED);
#endif
        while (1) { led_rgb(60,0,0); delay(400); led_rgb(0,0,0); delay(400); }
    }

    wifi_cap_init();

    // The 5 GHz capability probe is deferred to the first sweep the operator
    // starts. It is receive-only and processes no frames, but it does bring
    // the PHY up and hop channels, and "no automatic starts" is easier to
    // trust if boot genuinely touches no radio at all.
    Serial.println("5 GHz probe deferred until the first sweep starts");
    ble_cap_init();

    if (!log_init())
        Serial.println("SD not mounted - session will not be logged");

    // Reading characterisation files off the card is not a scan, so it is
    // allowed before the operator starts anything. Templates are written on
    // first run so the card documents its own format.
    if (s_sd_ok) {
#if !SERIAL_ONLY
        fill_screen(C_BLACK);
        draw_text_12(8, 100, "IMPORTING DATA...", C_CYAN, C_BLACK);
#endif
        import_write_templates();
        import_run(false);
    }

    memset(s_radio_ms, 0, sizeof(s_radio_ms));

#if !SERIAL_ONLY
    // First run, or a wiped NVS: calibrate touch before anything else. Guessed
    // raw limits are not worth having on a resistive panel, and an operator
    // whose taps land in the wrong place cannot even reach the SETUP screen to
    // fix it. One retry, then continue on defaults rather than trapping them
    // in a loop.
    if (!g_tcal.valid) {
        if (!touch_calibrate_interactive()) touch_calibrate_interactive();
    }

    // Colour order, confirmed by eye. It decides the colour of every alert,
    // and a red/blue swap leaves green alone - so ambient still looks right
    // while CONTACT renders blue and NEAR renders teal. Nothing appears
    // broken, which is why this is a question and not a #define.
    //
    // Touch calibration runs first, because answering this needs touch.
    if (!g_disp.confirmed) lcd_confirm_colour_order();
    // Radios stay off until a sweep is chosen on the welcome screen.
    sched_set_mode(MODE_IDLE);
    ui_set_screen(SCR_WELCOME);
#else
    // Headless builds wait for a command too, rather than sweeping on power-up.
    sched_set_mode(MODE_IDLE);
    Serial.println("idle - press a/w/b/t to start a sweep");
#endif

    xTaskCreate(pump_task, "pump", 8192, NULL, 5, NULL);

    Serial.println("ready");
    Serial.println("workflow: corridor baseline -> room baseline -> Wi-Fi pass -> BLE pass");
    Serial.println("serial cmds: a=sweep-all w=wifi b=ble t=watch n=next-pass");
    Serial.println("             m=mark p=phase l=list c=rssi-cal x=stop");
    led_rgb(0, 8, 0);
}

// Serial control, so the whole tool is usable even with a dead panel.
static void serial_cmds()
{
    if (!Serial.available()) return;
    int c = Serial.read();
    uint32_t now = millis();
    if (c == 'a') { sched_set_mode(MODE_SWEEP_ALL); Serial.println("> SWEEP ALL, pass 1/2 Wi-Fi"); }
    else if (c == 'w') { sched_set_mode(MODE_WIFI_SWEEP); Serial.println("> WI-FI SWEEP"); }
    else if (c == 'b') { sched_set_mode(MODE_BLE_SWEEP); Serial.println("> BLE SWEEP"); }
    else if (c == 't') { sched_set_mode(MODE_WATCH); Serial.println("> WATCH"); }
    else if (c == 'x') { sched_set_mode(MODE_IDLE); Serial.println("> radios stopped"); }
    else if (c == 'n') { sched_all_next_pass(now); Serial.println("> pass 2/2 BLE - walk the room again"); }
    else if (c == 'm') { sched_mark_position(now); Serial.println("> position marked"); }
    else if (c == 'c') { cal_advance(); Serial.printf("> CAL: %s\n", cal_prompt()); }
    else if (c == 'p') {
        if (g_phase == PHASE_FREE)          { baseline_set_phase(PHASE_CORRIDOR, now); Serial.println("> CORRIDOR 60s"); }
        else if (g_phase == PHASE_CORRIDOR) { baseline_set_phase(PHASE_ROOM, now);     Serial.println("> ROOM 60s"); }
        else { baseline_apply_diff(); baseline_set_phase(PHASE_FREE, now);
               log_snapshot(now, "BASELINE"); Serial.println("> diff applied"); }
    } else if (c == 'l') {
        static track_t *l[MAX_TRACKS];
        uint16_t n = tracks_sorted(l, MAX_TRACKS);
        Serial.printf("--- %s | %u tracks | radio %.1fs | covers <=%.2fs | pos %lu ---\n",
                      sched_mode_name(s_mode), (unsigned)n,
                      sched_position_radio_ms()/1000.0f,
                      sched_covered_interval_ms()/1000.0f,
                      (unsigned long)s_positions);
        for (uint16_t i = 0; i < n && i < 20; i++) {
            char ps[20]; log_pseudonym(l[i]->mac, ps, sizeof(ps));
            Serial.printf("%-8s %-4s %4ddBm s%-3u %s %.20s\n",
                          tier_name(l[i]->tier_best), band_name(l[i]->band),
                          (int)l[i]->peak_session, (unsigned)l[i]->score, ps,
                          l[i]->name);
        }
    }
}

void loop()
{
#if !SERIAL_ONLY
    ui_tick(millis());
#else
    // Headless: still drive the LED so the tier is visible.
    static track_t *l[8];
    uint16_t n = tracks_sorted(l, 8);
    tier_t tr = n ? l[0]->tier : TIER_AMBIENT;
    if (tr == TIER_CONTACT)   led_rgb(50,0,0);
    else if (tr == TIER_NEAR) led_rgb(45,22,0);
    else                      led_rgb(0,8,0);
#endif
    serial_cmds();
    delay(10);
}
