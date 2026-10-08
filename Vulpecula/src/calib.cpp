// calib.cpp - Guided RSSI threshold calibration.
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
// CALIBRATION  -  guided, two distances, per band
//
// The shipped thresholds are maths plus an assumed margin. Antenna choice,
// whether the 0-ohm RF selector actually routes to your U.FL socket, and
// board-to-board variation all move them several dB.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)


cal_state_t s_cal = CAL_IDLE;
rband_t  s_cal_band = BAND_24;
int8_t   s_cal_buf[CAL_MAX_SAMPLES];
uint16_t s_cal_n = 0;
int8_t   s_cal_near = 0, s_cal_contact = 0;

// dB to add to a measured reading to turn it into a threshold for a typical
// target. Zero when the beacon is set to the design-assumption power, which
// is the default and the least surprising case.
int cal_ref_correction(rband_t band)
{
    if (band == BAND_BLE) return CAL_TARGET_BLE_DBM - CAL_REF_BLE_DBM;
    return CAL_TARGET_WIFI_DBM - CAL_REF_WIFI_DBM;
}

int cmp_i8(const void *a, const void *b)
{ return (int)(*(const int8_t*)a) - (int)(*(const int8_t*)b); }

// The gate fires on peak, so the threshold should sit where the peak of a
// genuine target at the reference distance lands - not at its mean. The 80th
// percentile is high enough to represent peak behaviour, low enough that one
// multipath spike does not set the threshold.
int8_t cal_p80()
{
    if (!s_cal_n) return 0;
    int8_t tmp[CAL_MAX_SAMPLES];
    memcpy(tmp, s_cal_buf, s_cal_n);
    qsort(tmp, s_cal_n, 1, cmp_i8);
    uint16_t i = (uint16_t)((s_cal_n * 80) / 100);
    if (i >= s_cal_n) i = (uint16_t)(s_cal_n - 1);
    return tmp[i];
}

void cal_feed(int8_t rssi)
{
    if (s_cal != CAL_NEAR && s_cal != CAL_CONTACT) return;
    if (rssi == INT8_MIN) return;
    if (s_cal_n < CAL_MAX_SAMPLES) s_cal_buf[s_cal_n++] = rssi;
}

// Was the reference actually visible last time we looked? Surfaced on the
// page, because "no samples" needs to distinguish "not transmitting / wrong
// band / out of range" from "still collecting".
bool s_cal_ref_seen = false;
int8_t s_cal_last_rssi = 0;

// Find the reference beacon by name. Returns NULL if it is not being heard on
// the band under calibration.
track_t *cal_find_ref(void)
{
    for (uint16_t i = 0; i < MAX_TRACKS; i++) {
        track_t *t = track_at(i);
        if (!t || t->band != s_cal_band) continue;
        if (!t->name[0]) continue;
        if (strstr(t->name, CAL_REF_TAG)) return t;
    }
    return NULL;
}

// Start a calibration on an EXPLICIT band, and hold the radio there.
//
// The band used to be inherited from whatever the sweep happened to be doing,
// which during a dual-band Wi-Fi sweep alternates every 1.5 s - so it was a
// coin flip, and half the time it selected a band the beacon was not on and
// collected nothing at all.
void cal_begin(rband_t band)
{
    s_cal_band = band;
    s_cal = CAL_NEAR;
    s_cal_n = 0;
    s_cal_ref_seen = false;
    sched_cal_band(band);        // takes the radio and stops band rotation
}

void cal_advance()
{
    if (s_cal == CAL_IDLE)      { cal_begin(s_cal_band); }
    else if (s_cal == CAL_NEAR) {
        if (s_cal_n < CAL_MIN_SAMPLES) { s_cal = CAL_FAILED; sched_cal_release(); return; }
        // Normalise from "what this beacon reads" to "what a target reads".
        s_cal_near = (int8_t)(cal_p80() + cal_ref_correction(s_cal_band));
        s_cal_n = 0; s_cal = CAL_CONTACT;
    } else if (s_cal == CAL_CONTACT) {
        if (s_cal_n < CAL_MIN_SAMPLES) { s_cal = CAL_FAILED; return; }
        s_cal_contact = (int8_t)(cal_p80() + cal_ref_correction(s_cal_band));
        // Free space between 2.0 m and 0.3 m is 16.5 dB. Much less than 8 dB
        // means the reference never moved, or the antenna is not connected.
        if (s_cal_contact - s_cal_near < 8) { s_cal = CAL_FAILED; sched_cal_release(); return; }
        s_cal = CAL_DONE;
    } else if (s_cal == CAL_DONE) {
        g_cal.near_thresh[s_cal_band]    = s_cal_near;
        g_cal.contact_thresh[s_cal_band] = s_cal_contact;
        g_cal.calibrated[s_cal_band]     = true;
        g_cal.derived[s_cal_band]        = false;   // measured now
        prox_cal_save();
        // A 2.4 GHz-only reference is the common case, so carry the offset
        // across rather than leaving 5 GHz on an untouched default.
        if (s_cal_band == BAND_24) cal_derive_5g();
        s_cal = CAL_IDLE;
        sched_cal_release();
    } else { s_cal = CAL_IDLE; sched_cal_release(); }             // clear a failure
}

const char *cal_prompt()
{
    switch (s_cal) {
        case CAL_NEAR:    return "Ref at 2.0m. Hold still.";
        case CAL_CONTACT: return "Move ref to 0.3m. Hold.";
        case CAL_DONE:    return "Done - tap to commit.";
        case CAL_FAILED:  return "FAILED: few samples/bad spread";
        default:          return "Tap to calibrate this band";
    }
}

// ---------------------------------------------------------------------------
// Derive the 5 GHz thresholds from a measured 2.4 GHz calibration.
//
// WHY THIS EXISTS
//
// Only a dual-band reference can calibrate 5 GHz, and the spare ESP32 most
// people have is 2.4 GHz only. The shipped 5 GHz defaults assume zero
// implementation error - which is precisely the assumption that calibrating
// disproves. So once 2.4 GHz has been measured we know how far off this
// board's defaults actually are, and we shift 5 GHz by the same amount.
//
// WHAT THIS IS NOT
//
// A measurement. Antenna efficiency, matching and enclosure loss all differ
// between bands, so the offset only transfers the part of the error that is
// common to both - mostly the receiver chain and the implementation margin.
// It is better than an untouched default and worse than a real 5 GHz
// reference, and it is flagged as derived so nobody confuses the two.
// ---------------------------------------------------------------------------
void cal_derive_5g(void)
{
    if (!g_cal.calibrated[BAND_24]) return;   // nothing to derive from
    if (g_cal.calibrated[BAND_5]) return;     // measured beats derived

    int near_delta    = g_cal.near_thresh[BAND_24]    - THRESH_NEAR_24;
    int contact_delta = g_cal.contact_thresh[BAND_24] - THRESH_CONTACT_24;

    int n5 = THRESH_NEAR_5    + near_delta;
    int c5 = THRESH_CONTACT_5 + contact_delta;

    // Clamp to the same sanity window prox_init() enforces on a stored blob,
    // so a wild 2.4 GHz result cannot push 5 GHz somewhere unusable.
    if (n5 > -10) n5 = -10;
    if (n5 < -90) n5 = -90;
    if (c5 <= n5) c5 = n5 + 8;
    if (c5 > -5)  c5 = -5;

    g_cal.near_thresh[BAND_5]    = (int8_t)n5;
    g_cal.contact_thresh[BAND_5] = (int8_t)c5;
    g_cal.derived[BAND_5]        = true;
    g_cal.calibrated[BAND_5]     = false;     // derived is not calibrated
    prox_cal_save();

    Serial.printf("[cal] 5 GHz derived from the 2.4 GHz offset "
                  "(%+d/%+d dB): near %d, contact %d\n",
                  near_delta, contact_delta, n5, c5);
}
