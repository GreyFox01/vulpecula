/* ===========================================================================
   reference_beacon.ino
   ---------------------------------------------------------------------------
   Calibration transmitter for Vulpecula.

   WHY A KNOWN TRANSMITTER AND NOT A PHONE

   Calibration measures RSSI at a tape-measured distance and turns it into a
   gate threshold. That is only meaningful if you know what the source
   transmits. A phone or a router varies its power with thermal and link
   conditions, so the reading drifts and you cannot write its EIRP on the
   case. This sketch pins the power, reports what the radio actually accepted,
   and encodes the figure in the SSID so the two can never get out of step.

   ---------------------------------------------------------------------------
   BOARD

   A second NM-CYD-C5 is the right choice, because it is the only common part
   that can transmit on 5 GHz. Vulpecula needs a 5 GHz threshold, and nothing
   2.4-only can provide one.

   It will also run on an ESP32, S3, C3 or C6 - but those are 2.4 GHz only.
   Set REF_BAND to 24, calibrate 2.4 GHz and BLE, and let Vulpecula DERIVE the
   5 GHz threshold from the 2.4 GHz offset. SETUP marks derived thresholds D
   rather than M so the two are never confused. Better than an untouched
   default, worse than a real measurement.

   ---------------------------------------------------------------------------
   THE SCREEN STAYS DARK, DELIBERATELY

   On a CYD this looks like a dead board, so: the beacon is headless on
   purpose. Driving the display would mean duplicating ~700 lines of driver
   and font tables into a sketch whose whole value is being small enough to
   audit at a glance. Status goes to serial, and the RGB LED shows the band:

       BLUE    2.4 GHz
       MAGENTA 5 GHz
       RED     the radio refused the requested power - do not calibrate

   ---------------------------------------------------------------------------
   THE NAME IS DELIBERATELY ALARMING

   A calibration transmitter that could be mistaken for a real find during a
   later sweep is a hazard. DO-NOT-TRUST in the name means it can never
   quietly end up in a report as a detection.

   ---------------------------------------------------------------------------
   SETUP: Board ESP32C5 Dev Module, USB CDC On Boot ENABLED (or you get no
   serial output), 115200 baud.

   Serial commands:  2 = 2.4 GHz    5 = 5 GHz    s = status

   Copyright (c) 2026 The Wolf Creek Assembly
   SPDX-License-Identifier: MIT
   Licence: MIT. See LICENSE in the repository root.
   =========================================================================== */

#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>

// ---------------------------------------------------------------------------
// TRANSMIT POWER
//
// These default to the design assumptions Vulpecula's shipped thresholds were
// derived from: +17 dBm for a Wi-Fi camera, 0 dBm for a BLE device. Matching
// them means the detector's reference correction is zero, which is one fewer
// thing to get wrong.
//
// If you change these you MUST change CAL_REF_WIFI_DBM / CAL_REF_BLE_DBM in
// Vulpecula/src/config.h to match, or every threshold you measure will be
// wrong by the difference - silently, with plausible-looking numbers.
// ---------------------------------------------------------------------------
#define REF_WIFI_DBM   17
#define REF_BLE_DBM     0

// Starting band: 24 or 5. Switchable at runtime over serial, so you do not
// have to reflash between the 2.4 GHz and 5 GHz calibrations.
#define REF_BAND       24

// The SSID carries the Wi-Fi power, so the detector's operator can read what
// the reference transmits off the air instead of trusting a sticker.
// 32-character SSID limit: "VULP-CAL-W17-DO-NOT-TRUST" is 25.
#define REF_AP_PASS    "calibration"
#define PIN_WS2812     27

static int  s_band = REF_BAND;
static bool s_power_ok = true;
static char s_ssid[33];

static void led(uint8_t r, uint8_t g, uint8_t b)
{
    neopixelWrite(PIN_WS2812, r, g, b);
}

static void show_band_led()
{
    if (!s_power_ok)      led(60, 0, 0);     // refused power: do not calibrate
    else if (s_band == 5) led(40, 0, 40);    // magenta
    else                  led(0, 0, 50);     // blue
}

// ---------------------------------------------------------------------------
// Bring up the SoftAP on the requested band.
//
// The C5 cannot transmit on both bands at once, same constraint the detector
// works around. The band has to be selected before the AP starts, so the AP
// is stopped and restarted on a switch.
// ---------------------------------------------------------------------------
static bool start_ap(int band)
{
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_NULL);
    delay(120);
    WiFi.mode(WIFI_AP);

    // NOTE: the band-mode API arrived with C5 support. If your core exposes a
    // different symbol this is the one call to adjust. On a 2.4-only part it
    // will fail for BAND_MODE_5G, which is caught below.
    wifi_band_mode_t m = (band == 5) ? WIFI_BAND_MODE_5G_ONLY
                                     : WIFI_BAND_MODE_2G_ONLY;
    if (esp_wifi_set_band_mode(m) != ESP_OK) {
        Serial.printf("ERROR: this board cannot do %d GHz\n",
                      band == 5 ? 5 : 2);
        return false;
    }
    delay(50);

    snprintf(s_ssid, sizeof(s_ssid), "VULP-CAL-W%d-DO-NOT-TRUST", REF_WIFI_DBM);
    if (!WiFi.softAP(s_ssid, REF_AP_PASS)) {
        Serial.println("ERROR: softAP failed to start");
        return false;
    }

    // esp_wifi_set_max_tx_power takes quarter-dBm, and must be set after the
    // interface is up or it is silently discarded.
    if (esp_wifi_set_max_tx_power((int8_t)(REF_WIFI_DBM * 4)) != ESP_OK)
        Serial.println("WARNING: could not set Wi-Fi transmit power");

    s_band = band;
    return true;
}

static void report()
{
    int8_t got = 0;
    esp_wifi_get_max_tx_power(&got);
    float got_dbm = got * 0.25f;

    Serial.println();
    Serial.println("=== Vulpecula calibration reference ===");
    Serial.printf("  SSID      : %s\n", s_ssid);
    Serial.printf("  band      : %s\n", s_band == 5 ? "5 GHz" : "2.4 GHz");
    Serial.printf("  WiFi power: %.2f dBm asked, %.2f dBm accepted\n",
                  (float)REF_WIFI_DBM, got_dbm);
    Serial.printf("  BLE power : %d dBm asked, %d dBm accepted\n",
                  REF_BLE_DBM, (int)NimBLEDevice::getPower());
    Serial.printf("  BLE advert: 100 ms, TX Power Level field present\n");

    // A mismatch here is the difference between a correct gate and a
    // confidently wrong one, so it is called out rather than left in a log.
    if (fabsf(got_dbm - (float)REF_WIFI_DBM) > 1.0f) {
        s_power_ok = false;
        Serial.println();
        Serial.printf("  *** WI-FI POWER MISMATCH ***\n");
        Serial.printf("  The radio accepted %.2f dBm, not %d dBm.\n",
                      got_dbm, REF_WIFI_DBM);
        Serial.printf("  Set CAL_REF_WIFI_DBM to %d in Vulpecula's\n",
                      (int)lroundf(got_dbm));
        Serial.printf("  src/config.h before calibrating, or every threshold\n");
        Serial.printf("  will be wrong by %+.0f dB.\n",
                      (float)REF_WIFI_DBM - got_dbm);
    } else {
        s_power_ok = true;
    }

    Serial.println();
    Serial.printf("  commands: 2 = 2.4 GHz, 5 = 5 GHz, s = status\n");
    Serial.println("  Write the accepted power on the enclosure.");
    show_band_led();
}

void setup()
{
    Serial.begin(115200);
    delay(400);

    pinMode(PIN_WS2812, OUTPUT);
    led(30, 20, 0);                       // amber while starting

    // --- BLE: a legacy advert carrying an explicit TX Power Level field.
    // That field is the point: it exercises Vulpecula's reference-based BLE
    // ranging path rather than only the raw-RSSI fallback.
    NimBLEDevice::init("VULP-CAL-REF-DO-NOT-TRUST");
    // NimBLE 2.x takes plain dBm here, not esp_power_level_t.
    if (!NimBLEDevice::setPower(REF_BLE_DBM)) {
        Serial.println("WARNING: BLE setPower refused - verify the level");
        s_power_ok = false;
    }

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    data.setName("VULP-CAL-REF-DO-NOT-TRUST");
    data.addTxPower();                    // AD type 0x0A
    adv->setAdvertisementData(data);
    // 100 ms: fast, so the detector reaches 40 samples without a long wait.
    adv->setMinInterval(160);             // 160 * 0.625 ms
    adv->setMaxInterval(160);
    adv->start();

    if (!start_ap(s_band)) {
        // Fall back rather than sitting silent: a 2.4-only board asked for
        // 5 GHz should still be usable for the 2.4 GHz calibration.
        Serial.println("falling back to 2.4 GHz");
        start_ap(24);
    }
    report();
}

void loop()
{
    if (Serial.available()) {
        int c = Serial.read();
        if (c == '2')      { if (start_ap(24)) report(); }
        else if (c == '5') { if (start_ap(5))  report(); }
        else if (c == 's') report();
    }
    // Both radios beacon on their own. Kept awake rather than sleeping: a
    // reference that intermittently vanishes makes samples meaningless.
    delay(50);
}
