// sched.cpp - Radio time allocation, sweep passes, dwell accounting.
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
// SCHEDULER  -  separate passes, plus dwell accounting
//
// Three hard constraints on this chip:
//   1. no simultaneous dual-band - esp_wifi_set_band_mode picks one
//   2. one antenna on WROOM-1, software time-division, no GPIO switching
//   3. Wi-Fi and BLE also share time by coexistence
//
// So detection probability at a sweep position is radio-on dwell versus the
// target's transmit interval. BLE adverts range 20 ms to 10.24 s. A three-way
// round-robin at 33% duty needs 30+ s of wall clock per position to cover a
// 10 s advertiser - unusable while walking.
//
// MODE_SWEEP_ALL is therefore NOT a simultaneous sweep, because the hardware
// cannot do one. It is two sequential passes with an explicit prompt between
// them: Wi-Fi across the whole room, then BLE across the whole room again.
// Two fast walks beat one slow one. Round-robin survives only in WATCH, where
// the board sits still and dwell is unbounded.
// ===========================================================================

sweep_mode_t s_mode = MODE_IDLE;
rband_t  s_active = BAND_24;
uint8_t  s_ch24_idx = 0, s_ch5_idx = 0, s_watch_phase = 0;
uint32_t s_last_hop = 0, s_band_slice = 0, s_watch_slice = 0;
uint32_t s_pos_start = 0, s_radio_ms[BAND_COUNT], s_last_accrue = 0;
int8_t   s_last_strongest = INT8_MIN;
uint32_t s_positions = 0;

// SWEEP ALL state: phase 0 = Wi-Fi pass, phase 1 = BLE pass.
uint8_t  s_all_phase = 0;
bool     s_pass_prompt = false;
uint32_t s_pass_radio_ms = 0;
// Auto-advance from the Wi-Fi pass to the BLE pass after this much radio time,
// so an operator who never taps the button still gets both passes. Generous:
// a room takes a couple of minutes to walk properly.

const char *sched_mode_name(sweep_mode_t m)
{
    return m == MODE_WIFI_SWEEP ? "WI-FI"
         : m == MODE_BLE_SWEEP  ? "BLE"
         : m == MODE_SWEEP_ALL  ? "ALL"
         : m == MODE_WATCH      ? "WATCH" : "IDLE";
}

// Which protocol currently owns the radio.
rband_t sched_active_proto()
{
    if (s_mode == MODE_BLE_SWEEP) return BAND_BLE;
    if (s_mode == MODE_SWEEP_ALL && s_all_phase == 1) return BAND_BLE;
    if (s_mode == MODE_WATCH && s_watch_phase == 2) return BAND_BLE;
    return s_active;
}

// Compact status for the sweep banner, e.g. "ALL 1/2 2.4G" or "BLE".
const char *sched_status()
{
    static char buf[20];
    switch (s_mode) {
        case MODE_SWEEP_ALL:
            if (s_all_phase == 0)
                snprintf(buf, sizeof(buf), "ALL 1/2 %s", band_name(s_active));
            else
                snprintf(buf, sizeof(buf), "ALL 2/2 BLE");
            break;
        case MODE_WIFI_SWEEP:
            snprintf(buf, sizeof(buf), "WI-FI %s", band_name(s_active));
            break;
        case MODE_BLE_SWEEP:
            snprintf(buf, sizeof(buf), "BLE");
            break;
        case MODE_WATCH:
            snprintf(buf, sizeof(buf), "WATCH %s", band_name(sched_active_proto()));
            break;
        default:
            snprintf(buf, sizeof(buf), "IDLE");
            break;
    }
    return buf;
}

void sched_mark_position(uint32_t now)
{
    // Snapshot per-position peaks into each track's sweep profile before
    // resetting, so the peak-shape classifier has data to work with.
    if (s_pos_start) { tracks_close_position(now); s_positions++; }
    s_pos_start = now;
    s_last_accrue = now;
    memset(s_radio_ms, 0, sizeof(s_radio_ms));
    s_last_strongest = INT8_MIN;
}

// Probed once, on demand, the first time Wi-Fi is actually used.
bool s_probed_5g = false;

void ensure_5g_probed()
{
    if (s_probed_5g) return;
    s_probed_5g = true;
    Serial.println("probing 5 GHz channels...");
    wifi_cap_probe_5g();
    Serial.printf("5 GHz usable: %u channels, DFS %s\n", s_n_ch5,
                  s_dfs_ok ? "yes" : "no");
    if (s_n_ch5 == 0) {
        // Worth shouting about: 5 GHz coverage is the whole reason to build
        // this on a C5, and most other ESP32 detectors cannot see a 5 GHz
        // camera at all.
        Serial.println("WARNING: no 5 GHz - running 2.4 GHz only");
    }
}

void radios_wifi_only()
{
    ensure_5g_probed();
    ble_cap_stop();
    wifi_cap_start();
    if (wifi_cap_set_band(BAND_24)) s_active = BAND_24;
    wifi_cap_set_channel(CH_24[0]);
}

void radios_ble_only()
{
    wifi_cap_stop();
    s_active = BAND_BLE;
    ble_cap_start();
}

void sched_set_mode(sweep_mode_t m)
{
    if (m == s_mode) return;
    wifi_cap_stop();
    ble_cap_stop();
    s_mode = m;

    uint32_t now = millis();
    s_band_slice = s_watch_slice = s_last_hop = now;
    s_watch_phase = 0;
    s_all_phase = 0;
    s_pass_radio_ms = 0;
    s_pass_prompt = false;
    s_positions = 0;
    s_pos_start = 0;
    sched_mark_position(now);

    if (m == MODE_WIFI_SWEEP || m == MODE_WATCH || m == MODE_SWEEP_ALL)
        radios_wifi_only();
    else if (m == MODE_BLE_SWEEP)
        radios_ble_only();
    else
        led_rgb(0, 0, 20);      // idle: blue
}

// Roll SWEEP ALL from the Wi-Fi pass to the BLE pass. The prompt matters: the
// operator has to physically walk the room a second time, and without being
// told they will assume one lap covered everything.
void sched_all_next_pass(uint32_t now)
{
    if (s_mode != MODE_SWEEP_ALL || s_all_phase != 0) return;
    s_all_phase = 1;
    s_pass_radio_ms = 0;
    s_pass_prompt = true;
    radios_ble_only();
    sched_mark_position(now);
}

void sched_accrue(uint32_t now)
{
    if (!s_last_accrue) { s_last_accrue = now; return; }
    uint32_t dt = now - s_last_accrue;
    s_last_accrue = now;
    if (s_mode == MODE_IDLE) return;
    rband_t p = sched_active_proto();
    if (p < BAND_COUNT) s_radio_ms[p] += dt;
    s_pass_radio_ms += dt;
}

uint32_t sched_position_radio_ms()
{
    switch (s_mode) {
        case MODE_BLE_SWEEP: return s_radio_ms[BAND_BLE];
        case MODE_WIFI_SWEEP: return s_radio_ms[BAND_24] + s_radio_ms[BAND_5];
        case MODE_SWEEP_ALL:
            return s_all_phase == 0 ? s_radio_ms[BAND_24] + s_radio_ms[BAND_5]
                                    : s_radio_ms[BAND_BLE];
        case MODE_WATCH: {
            // The meaningful figure is the worst-served protocol, since that
            // is what actually bounds coverage.
            uint32_t worst = s_radio_ms[0];
            for (int b = 1; b < BAND_COUNT; b++)
                if (s_radio_ms[b] < worst) worst = s_radio_ms[b];
            return worst;
        }
        default: return 0;
    }
}

// The longest transmit interval we can honestly claim to cover here: radio
// time divided by the packets the gate requires.
uint32_t sched_covered_interval_ms()
{
    uint32_t r = sched_position_radio_ms();
    return r ? r / MIN_PKTS_FOR_NEAR : 0;
}

bool sched_sweep_too_fast()
{
    if (s_mode == MODE_IDLE || !s_pos_start) return false;
    return sched_position_radio_ms() < SWEEP_WARN_BELOW_MS;
}

// Movement proxy. There is no IMU, so if the strongest thing in earshot
// changed level substantially the operator almost certainly moved. Only
// auto-remark once the position has earned enough radio time to be worth
// recording, or a noisy room would shred the sweep profile.
void sched_note_strongest(int8_t peak, uint32_t now)
{
    if (peak == INT8_MIN) return;
    if (s_last_strongest == INT8_MIN) { s_last_strongest = peak; return; }
    if (abs((int)peak - (int)s_last_strongest) >= AUTO_REMARK_DELTA_DB &&
        sched_position_radio_ms() >= SWEEP_WARN_BELOW_MS) {
        sched_mark_position(now);
        s_last_strongest = peak;
    } else if (peak > s_last_strongest) s_last_strongest = peak;
}

// While a calibration is running the radio is pinned to one band, and on
// 5 GHz to one channel. Rotation would otherwise spend most of the dwell
// elsewhere: the beacon occupies ONE of up to 25 channels, so hopping them
// all leaves the reference heard about 4% of the time - indistinguishable
// from a dead beacon.
bool s_band_locked = false;

void sched_cal_band(rband_t band)
{
    s_band_locked = true;
    if (band == BAND_BLE) {
        radios_ble_only();
    } else {
        ble_cap_stop();
        wifi_cap_start();
        ensure_5g_probed();
        if (wifi_cap_set_band(band)) s_active = band;
        if (band == BAND_5 && s_n_ch5) wifi_cap_set_channel(s_ch5[0]);
        else                           wifi_cap_set_channel(CH_24[0]);
    }
    Serial.printf("[cal] radio locked to %s\n", band_name(band));
}

// Pin to the channel the reference is actually on, once it has been found.
void sched_cal_channel(uint8_t ch)
{
    if (!s_band_locked) return;
    wifi_cap_set_channel(ch);
}

void sched_cal_release(void)
{
    if (!s_band_locked) return;
    s_band_locked = false;
    Serial.println("[cal] radio released");
}

void wifi_rotate(uint32_t now)
{
    if ((uint32_t)(now - s_last_hop) >= WIFI_CH_DWELL_MS) {
        s_last_hop = now;
        if (s_active == BAND_24 && !s_band_locked) {
            s_ch24_idx = (uint8_t)((s_ch24_idx + 1) % (sizeof(CH_24)/sizeof(CH_24[0])));
            wifi_cap_set_channel(CH_24[s_ch24_idx]);
        } else if (s_n_ch5 && !s_band_locked) {
            s_ch5_idx = (uint8_t)((s_ch5_idx + 1) % s_n_ch5);
            wifi_cap_set_channel(s_ch5[s_ch5_idx]);
        }
    }
    // Band switching is not free, so swap on a coarse slice, not per channel.
    // Suppressed while a calibration holds the radio on one band.
    if (!s_band_locked && (uint32_t)(now - s_band_slice) >= WIFI_BAND_SLICE_MS) {
        s_band_slice = now;
        if (s_active == BAND_24 && s_n_ch5) {
            if (wifi_cap_set_band(BAND_5)) {
                s_active = BAND_5; s_ch5_idx = 0; wifi_cap_set_channel(s_ch5[0]);
            }
        } else if (wifi_cap_set_band(BAND_24)) {
            s_active = BAND_24; s_ch24_idx = 0; wifi_cap_set_channel(CH_24[0]);
        }
        s_last_hop = now;
    }
}

void watch_rotate(uint32_t now)
{
    if ((uint32_t)(now - s_watch_slice) < WATCH_SLICE_MS) {
        if (s_watch_phase != 2) wifi_rotate(now);
        return;
    }
    s_watch_slice = now;
    s_watch_phase = (uint8_t)((s_watch_phase + 1) % 3);
    if (s_watch_phase == 0) {
        radios_wifi_only();
    } else if (s_watch_phase == 1) {
        ble_cap_stop();
        wifi_cap_start();
        if (s_n_ch5 && wifi_cap_set_band(BAND_5)) {
            s_active = BAND_5; wifi_cap_set_channel(s_ch5[0]);
        }
    } else {
        radios_ble_only();
    }
    s_last_hop = now;
}

void sched_tick(uint32_t now)
{
    sched_accrue(now);

    switch (s_mode) {
        case MODE_WIFI_SWEEP:
            wifi_rotate(now);
            break;
        case MODE_SWEEP_ALL:
            if (s_all_phase == 0) {
                wifi_rotate(now);
                if (s_pass_radio_ms >= ALL_PASS_AUTO_MS) sched_all_next_pass(now);
            }
            break;
        case MODE_WATCH:
            watch_rotate(now);
            break;
        default:
            break;      // BLE_SWEEP has nothing to rotate: BLE owns the radio
    }
}
