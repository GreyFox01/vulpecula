// proximity.cpp - Proximity gate. PURE: no Arduino dependency, so test/gate_test.cpp
// includes it directly instead of slicing it out of the sketch.
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

#include "pure.h"

// ===========================================================================
// PROXIMITY GATE
//
// Logic here is byte-identical to the version verified by test_gate.cpp on a
// PC (link budget, hysteresis, the 3-packet rule, BLE reference handling).
// If you change anything in this section, re-run that test.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

prox_cal_t g_cal;

void prox_cal_reset_defaults()
{
    memset(&g_cal, 0, sizeof(g_cal));
    g_cal.near_thresh[BAND_24]     = THRESH_NEAR_24;
    g_cal.contact_thresh[BAND_24]  = THRESH_CONTACT_24;
    g_cal.near_thresh[BAND_5]      = THRESH_NEAR_5;
    g_cal.contact_thresh[BAND_5]   = THRESH_CONTACT_5;
    g_cal.near_thresh[BAND_BLE]    = THRESH_NEAR_BLE;
    g_cal.contact_thresh[BAND_BLE] = THRESH_CONTACT_BLE;
    g_cal.ble_1m_loss              = BLE_1M_LOSS_DB;
    g_cal.pathloss_n               = PATHLOSS_EXPONENT;
}

void ring_reset(rssi_ring_t *r) { r->head = 0; r->count = 0; }

void ring_push(rssi_ring_t *r, int8_t rssi, uint32_t now)
{
    r->s[r->head].rssi = rssi;
    r->s[r->head].ts   = now;
    r->head = (uint8_t)((r->head + 1) % RSSI_RING_LEN);
    if (r->count < RSSI_RING_LEN) r->count++;
}

uint8_t ring_window(const rssi_ring_t *r, uint32_t now, uint32_t win,
                           int8_t *out, uint8_t cap)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < r->count && n < cap; i++) {
        uint8_t idx = (uint8_t)((r->head + RSSI_RING_LEN - 1 - i) % RSSI_RING_LEN);
        if ((uint32_t)(now - r->s[idx].ts) > win) break;   // rollover-safe
        out[n++] = r->s[idx].rssi;
    }
    return n;
}

int8_t ring_peak(const rssi_ring_t *r, uint32_t now, uint32_t win)
{
    int8_t b[RSSI_RING_LEN];
    uint8_t n = ring_window(r, now, win, b, RSSI_RING_LEN);
    if (!n) return INT8_MIN;
    int8_t pk = b[0];
    for (uint8_t i = 1; i < n; i++) if (b[i] > pk) pk = b[i];
    return pk;
}

int8_t ring_median(const rssi_ring_t *r, uint32_t now, uint32_t win)
{
    int8_t b[RSSI_RING_LEN];
    uint8_t n = ring_window(r, now, win, b, RSSI_RING_LEN);
    if (!n) return INT8_MIN;
    for (uint8_t i = 1; i < n; i++) {
        int8_t v = b[i]; int8_t j = (int8_t)(i - 1);
        while (j >= 0 && b[j] > v) { b[j+1] = b[j]; j--; }
        b[j+1] = v;
    }
    return b[n / 2];
}

uint8_t ring_count_above(const rssi_ring_t *r, uint32_t now,
                                uint32_t win, int8_t th)
{
    int8_t b[RSSI_RING_LEN];
    uint8_t n = ring_window(r, now, win, b, RSSI_RING_LEN), c = 0;
    for (uint8_t i = 0; i < n; i++) if (b[i] >= th) c++;
    return c;
}

// --- BLE calibrated references --------------------------------------------
ranging_ref_t prox_ref_from_txpower(int8_t tx_dbm)
{
    ranging_ref_t r;
    r.src = REF_ADV_TXPOWER;
    r.rssi_at_1m = (int8_t)(tx_dbm - g_cal.ble_1m_loss);
    return r;
}
ranging_ref_t prox_ref_from_ibeacon(int8_t measured_power)
{
    // iBeacon's measured power IS the calibrated RSSI at 1 m: no
    // implementation-margin guesswork at all, so this is the best reference.
    ranging_ref_t r; r.src = REF_IBEACON; r.rssi_at_1m = measured_power; return r;
}
ranging_ref_t prox_ref_from_eddystone(int8_t ranging_power)
{
    // Eddystone ranging data is power at 0 m; the spec's own conversion to a
    // 1 m reference is a 41 dB offset.
    ranging_ref_t r; r.src = REF_EDDYSTONE;
    r.rssi_at_1m = (int8_t)(ranging_power - 41); return r;
}

float rssi_to_distance(int8_t rssi, int8_t at1m, float n)
{ return powf(10.0f, ((float)at1m - (float)rssi) / (10.0f * n)); }

int8_t distance_to_rssi(float d, int8_t at1m, float n)
{
    if (d <= 0.01f) d = 0.01f;
    return (int8_t)lrintf((float)at1m - 10.0f * n * log10f(d));
}

float prox_distance_m(const rssi_ring_t *r, const ranging_ref_t *ref,
                             uint32_t now)
{
    if (!ref || ref->src == REF_NONE) return -1.0f;
    int8_t med = ring_median(r, now, MEDIAN_WINDOW_MS);
    if (med == INT8_MIN) return -1.0f;
    return rssi_to_distance(med, ref->rssi_at_1m, g_cal.pathloss_n);
}

tier_t prox_resolve_tier(const rssi_ring_t *r, rband_t band,
                                const ranging_ref_t *ref, tier_t prev,
                                uint32_t now)
{
    if (band >= BAND_COUNT) return TIER_AMBIENT;

    int8_t near_t, contact_t;
    if (ref && ref->src != REF_NONE) {
        // Derive thresholds from the distance targets, which removes transmit
        // power as an error term. This is the fix for BLE, where source power
        // spans ~28 dB and would otherwise swamp the wall margin entirely.
        near_t    = distance_to_rssi(DIST_NEAR_M,    ref->rssi_at_1m, g_cal.pathloss_n);
        contact_t = distance_to_rssi(DIST_CONTACT_M, ref->rssi_at_1m, g_cal.pathloss_n);
    } else {
        near_t    = g_cal.near_thresh[band];
        contact_t = g_cal.contact_thresh[band];
    }

    // Hysteresis applies only to the tier we are already in.
    if (prev >= TIER_NEAR)    near_t    = (int8_t)(near_t    - TIER_HYSTERESIS_DB);
    if (prev >= TIER_CONTACT) contact_t = (int8_t)(contact_t - TIER_HYSTERESIS_DB);

    // Gate on PEAK. A real 2 m device produces genuinely strong packets; a
    // median gets dragged into the floor by multipath nulls and you would
    // reject things sitting right in front of you.
    int8_t pk = ring_peak(r, now, PEAK_WINDOW_MS);
    if (pk == INT8_MIN) return TIER_AMBIENT;

    if (pk >= contact_t &&
        ring_count_above(r, now, PEAK_WINDOW_MS, contact_t) >= MIN_PKTS_FOR_NEAR)
        return TIER_CONTACT;
    if (pk >= near_t &&
        ring_count_above(r, now, PEAK_WINDOW_MS, near_t) >= MIN_PKTS_FOR_NEAR)
        return TIER_NEAR;
    return TIER_AMBIENT;
}

const char *tier_name(tier_t t)
{
    return t == TIER_CONTACT ? "CONTACT" : t == TIER_NEAR ? "NEAR" : "ambient";
}
const char *band_name(rband_t b)
{
    return b == BAND_24 ? "2.4G" : b == BAND_5 ? "5G" : b == BAND_BLE ? "BLE" : "?";
}
uint16_t tier_colour(tier_t t)
{
    return t == TIER_CONTACT ? C_RED : t == TIER_NEAR ? C_AMBER : C_GREEN;
}
