// touch.cpp - XPT2046 resistive touch and the guided 4-point calibration.
// ---------------------------------------------------------------------------
// This file belongs in Vulpecula/src/ and is compiled as part of the
// Vulpecula sketch. If it has been copied into another sketch folder, Arduino
// will try to build it there - because it compiles EVERY source file in a
// sketch folder - and the error you get points at a missing header rather
// than at the real problem.
// ---------------------------------------------------------------------------
#if !__has_include("vulpecula.h") && !__has_include("pure.h")
#error "Vulpecula module in the wrong folder. It must live in Vulpecula/src/ alongside vulpecula.h. Arduino compiles every source file in a sketch folder, so a copy left anywhere else gets built by mistake. Run test/sketch_hygiene_check.py."
#endif

#include "vulpecula.h"

// ===========================================================================
// TOUCH  -  XPT2046 resistive, shares the SPI bus, own CS on GPIO 1
//
// The first build of this firmware mapped raw to screen like this:
//
//     mx = map(ax, X_MIN, X_MAX, 0, W-1);
//     my = map(ay, Y_MIN, Y_MAX, 0, H-1);
//     if (swap) { long t = mx; mx = my * W / H; my = t * H / W; }
//
// which is wrong twice over. It mapped each raw axis against the wrong screen
// extent and THEN swapped and rescaled, compounding the error, so touches
// landed nowhere near the finger. The axes must be swapped in the RAW domain,
// before either is mapped, because on this panel the controller's X axis runs
// along the display's Y when the panel is rotated to landscape.
//
// It also relied on hard-coded raw limits. Resistive panels vary enough
// between units that guessed constants are not worth having, so the mapping
// is now MEASURED by a guided 4-point routine and persisted to NVS. The
// routine also determines the swap and both inversions empirically rather
// than asking anyone to guess which of the eight orientations applies.
// ===========================================================================

touch_cal_t g_tcal;
volatile uint16_t g_touch_raw_x = 0, g_touch_raw_y = 0, g_touch_raw_z = 0;

const char *TCAL_KEY = "touch_cal";

void touch_cal_defaults()
{
    // Deliberately marked invalid: an uncalibrated build must run the guided
    // routine rather than quietly using numbers nobody measured. These values
    // are only a fallback so the UI is reachable if NVS fails.
    g_tcal.x_min = 300;  g_tcal.x_max = 3800;
    g_tcal.y_min = 300;  g_tcal.y_max = 3800;
    g_tcal.swap  = true; g_tcal.inv_x = false; g_tcal.inv_y = true;
    g_tcal.valid = false;
}

void touch_cal_load()
{
    touch_cal_defaults();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(touch_cal_t);
    touch_cal_t st;
    if (nvs_get_blob(h, TCAL_KEY, &st, &len) == ESP_OK && len == sizeof(st)) {
        // Reject a stored blob whose spans are too small to be a real
        // calibration - a degenerate range would make every touch land in one
        // corner, which is worse than falling back to the guided routine.
        if (st.x_max > st.x_min + 400 && st.y_max > st.y_min + 400) g_tcal = st;
    }
    nvs_close(h);
}

void touch_cal_save()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, TCAL_KEY, &g_tcal, sizeof(touch_cal_t));
    nvs_commit(h);
    nvs_close(h);
}

uint16_t xpt_xfer(uint8_t cmd)
{
    SPI.transfer(cmd);
    uint8_t hi = SPI.transfer(0x00);
    uint8_t lo = SPI.transfer(0x00);
    return (uint16_t)(((hi << 8) | lo) >> 3);      // 12-bit result
}

// Raw read with pressure gating and median-of-5 on each axis. A single XPT2046
// sample is noisy enough to jump tens of counts, which on a 320px axis is
// several pixels of jitter - enough to make a button feel unreliable.
bool touch_raw(uint16_t *rx, uint16_t *ry)
{
    uint16_t xs[5], ys[5];

    spi_take();
    SPI.beginTransaction(SPI_TCH);
    digitalWrite(PIN_TOUCH_CS, LOW);

    uint16_t z1 = xpt_xfer(0xB1);
    uint16_t z2 = xpt_xfer(0xC1);
    uint16_t z  = (uint16_t)(z1 + 4095 - z2);

    for (int i = 0; i < 5; i++) {
        ys[i] = xpt_xfer(0x91);
        xs[i] = xpt_xfer(0xD1);
    }

    digitalWrite(PIN_TOUCH_CS, HIGH);
    SPI.endTransaction();
    spi_give();

    // median of 5, insertion sort
    for (int i = 1; i < 5; i++) {
        uint16_t a = xs[i], b = ys[i]; int j = i - 1;
        while (j >= 0 && xs[j] > a) { xs[j+1] = xs[j]; j--; } xs[j+1] = a;
        j = i - 1;
        while (j >= 0 && ys[j] > b) { ys[j+1] = ys[j]; j--; } ys[j+1] = b;
    }

    g_touch_raw_x = xs[2];
    g_touch_raw_y = ys[2];
    g_touch_raw_z = z;

    if (z < TOUCH_Z_THRESH) return false;
    if (xs[2] < 16 || ys[2] < 16 || xs[2] > 4080 || ys[2] > 4080) return false;

    *rx = xs[2];
    *ry = ys[2];
    return true;
}

bool touch_read(uint16_t *sx, uint16_t *sy)
{
    uint16_t rx, ry;
    if (!touch_raw(&rx, &ry)) return false;

    // Swap in the RAW domain, before mapping. This is the fix.
    uint16_t ax = g_tcal.swap ? ry : rx;
    uint16_t ay = g_tcal.swap ? rx : ry;

    long mx = map((long)ax, g_tcal.x_min, g_tcal.x_max, 0, SCR_W - 1);
    long my = map((long)ay, g_tcal.y_min, g_tcal.y_max, 0, SCR_H - 1);

    if (g_tcal.inv_x) mx = SCR_W - 1 - mx;
    if (g_tcal.inv_y) my = SCR_H - 1 - my;

    if (mx < 0) mx = 0;
    if (mx > SCR_W - 1) mx = SCR_W - 1;
    if (my < 0) my = 0;
    if (my > SCR_H - 1) my = SCR_H - 1;

    *sx = (uint16_t)mx;
    *sy = (uint16_t)my;
    return true;
}

// Wait for a stable press and return its raw coordinates. Requires the finger
// to be lifted first, so consecutive targets cannot be satisfied by one long
// press, and averages a short burst once pressure is steady.
bool touch_await_press(uint16_t *rx, uint16_t *ry, uint32_t timeout_ms)
{
    uint32_t t0 = millis();
    uint16_t a, b;

    while (touch_raw(&a, &b)) {                       // wait for release
        if ((uint32_t)(millis() - t0) > timeout_ms) return false;
        delay(20);
    }
    delay(80);

    while (!touch_raw(&a, &b)) {                      // wait for press
        if ((uint32_t)(millis() - t0) > timeout_ms) return false;
        delay(10);
    }

    delay(60);                                        // let pressure settle
    uint32_t sx = 0, sy = 0; int n = 0;
    for (int i = 0; i < 12; i++) {
        if (touch_raw(&a, &b)) { sx += a; sy += b; n++; }
        delay(10);
    }
    if (n < 6) return false;
    *rx = (uint16_t)(sx / n);
    *ry = (uint16_t)(sy / n);
    return true;
}

// ---------------------------------------------------------------------------
// Guided 4-point calibration.
//
// Draws a crosshair at four inset corners and records the raw reading at each.
// From those four samples it works out:
//   * whether the controller axes are transposed, by checking which raw axis
//     varies more between two points separated horizontally on screen
//   * the raw span of each axis, extrapolated from the inset to the full
//     screen so the edges remain reachable
//   * whether each axis runs forwards or backwards
// Nothing is guessed and nothing has to be entered by hand.
// ---------------------------------------------------------------------------

bool touch_calibrate_interactive()
{
    const int16_t px[4] = { TCAL_INSET, SCR_W - 1 - TCAL_INSET,
                            SCR_W - 1 - TCAL_INSET, TCAL_INSET };
    const int16_t py[4] = { TCAL_INSET, TCAL_INSET,
                            SCR_H - 1 - TCAL_INSET, SCR_H - 1 - TCAL_INSET };
    uint16_t rx[4], ry[4];

    for (int i = 0; i < 4; i++) {
        fill_screen(C_BLACK);
        draw_text_12(40, 24, "TOUCH CALIBRATION", C_CYAN, C_BLACK);
        draw_text(40, 52, "Tap the centre of each crosshair.", C_WHITE, C_BLACK);
        draw_text(40, 66, "Use a stylus or a fingernail if you can.", C_DIM, C_BLACK);
        char b[32];
        snprintf(b, sizeof(b), "point %d of 4", i + 1);
        draw_text(40, 88, b, C_DIM, C_BLACK);

        // crosshair
        draw_hline(px[i] - 14, py[i], 29, C_AMBER);
        draw_vline(px[i], py[i] - 14, 29, C_AMBER);
        fill_rect(px[i] - 2, py[i] - 2, 5, 5, C_RED);

        if (!touch_await_press(&rx[i], &ry[i], 30000)) {
            fill_screen(C_BLACK);
            draw_text_12(20, 100, "CALIBRATION TIMED OUT", C_RED, C_BLACK);
            draw_text(20, 126, "Using previous values. Retry from SETUP.", C_WHITE, C_BLACK);
            delay(2500);
            return false;
        }
        delay(150);
    }

    // Points 0 and 1 differ in screen X only. Whichever raw axis moved more
    // between them is the axis that tracks screen X.
    long dx_rawx = labs((long)rx[1] - (long)rx[0]);
    long dx_rawy = labs((long)ry[1] - (long)ry[0]);
    bool swap = (dx_rawy > dx_rawx);

    // Collect each point's raw value on the axis that corresponds to screen X
    // and screen Y, applying the swap we just determined.
    long ax[4], ay[4];
    for (int i = 0; i < 4; i++) {
        ax[i] = swap ? ry[i] : rx[i];
        ay[i] = swap ? rx[i] : ry[i];
    }

    // Average the two points at each screen edge.
    long x_left   = (ax[0] + ax[3]) / 2;    // screen x = TCAL_INSET
    long x_right  = (ax[1] + ax[2]) / 2;    // screen x = W-1-TCAL_INSET
    long y_top    = (ay[0] + ay[1]) / 2;    // screen y = TCAL_INSET
    long y_bottom = (ay[2] + ay[3]) / 2;    // screen y = H-1-TCAL_INSET

    bool inv_x = (x_right < x_left);
    bool inv_y = (y_bottom < y_top);
    if (inv_x) { long t = x_left; x_left = x_right; x_right = t; }
    if (inv_y) { long t = y_top;  y_top  = y_bottom; y_bottom = t; }

    // Extrapolate from the inset targets out to the true screen edges, or the
    // outer 28px of the display would be unreachable.
    long span_x = x_right - x_left, span_y = y_bottom - y_top;
    long used_x = (SCR_W - 1) - 2 * TCAL_INSET;
    long used_y = (SCR_H - 1) - 2 * TCAL_INSET;
    if (span_x < 200 || span_y < 200) {
        fill_screen(C_BLACK);
        draw_text_12(16, 96, "CALIBRATION FAILED", C_RED, C_BLACK);
        draw_text(16, 122, "Readings too close together. Tap the", C_WHITE, C_BLACK);
        draw_text(16, 136, "crosshair centres, not the same spot.", C_WHITE, C_BLACK);
        delay(3000);
        return false;
    }
    long per_x = span_x / used_x, per_y = span_y / used_y;

    g_tcal.x_min = (uint16_t)(x_left   - per_x * TCAL_INSET);
    g_tcal.x_max = (uint16_t)(x_right  + per_x * TCAL_INSET);
    g_tcal.y_min = (uint16_t)(y_top    - per_y * TCAL_INSET);
    g_tcal.y_max = (uint16_t)(y_bottom + per_y * TCAL_INSET);
    g_tcal.swap  = swap;
    g_tcal.inv_x = inv_x;
    g_tcal.inv_y = inv_y;
    g_tcal.valid = true;
    touch_cal_save();

    // Immediate verification: a dot must appear under the finger. This is the
    // whole point - the operator confirms the mapping works before relying on
    // it, rather than discovering mid-sweep that buttons do not respond.
    fill_screen(C_BLACK);
    draw_text_12(28, 18, "CHECK THE MAPPING", C_GREEN, C_BLACK);
    draw_text(28, 44, "Drag around. The dot should sit under", C_WHITE, C_BLACK);
    draw_text(28, 58, "your finger, and the corners must work.", C_WHITE, C_BLACK);
    draw_text(28, 80, "Tap the box to accept. Wait 12s to redo.", C_AMBER, C_BLACK);
    draw_rect(110, 180, 100, 34, C_CYAN);
    draw_text(140, 193, "ACCEPT", C_CYAN, C_BLACK);

    uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 12000) {
        uint16_t tx, ty;
        if (touch_read(&tx, &ty)) {
            fill_rect((int16_t)tx - 2, (int16_t)ty - 2, 5, 5, C_GREEN);
            if (tx >= 110 && tx <= 210 && ty >= 180 && ty <= 214) {
                delay(200);
                return true;
            }
        }
        delay(15);
    }
    return false;    // no accept -> caller offers another attempt
}

// ===========================================================================
// RGB LED  -  tier at a glance without looking at the screen
// ===========================================================================
void led_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    neopixelWrite(PIN_WS2812, r, g, b);
}
