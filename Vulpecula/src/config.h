// config.h - every tunable in one place.
//
// Split out of the former single-file sketch. See src/README.md for why the
// firmware now lives in src/ rather than in the .ino.
#pragma once
#include <stdint.h>
#include <stddef.h>

// ===========================================================================
// BOARD  -  see PIN PROVENANCE above before editing
// ===========================================================================
#define PIN_SCK        6
#define PIN_MISO       2
#define PIN_MOSI       7
#define PIN_LCD_CS    23
#define PIN_LCD_DC    24     // issue #3, not the vendor pinout table
#define PIN_LCD_RST   -1     // no reset line; software reset instead
#define PIN_TOUCH_CS   1
#define PIN_SD_CS     10
#define PIN_BL        25
#define PIN_WS2812    27

#define SPI_HZ_LCD    20000000
#define SPI_HZ_TOUCH   2500000
#define SPI_HZ_SD     20000000

#define PANEL_DRIVER  0       // 0 = ST7789 (default), 1 = ILI9341
// Only the STARTING guess for colour order. The operator confirms it by eye
// at first boot and the answer is stored in NVS, because a red/blue swap
// leaves green alone - so ambient still looks right while CONTACT renders
// blue and NEAR renders teal, and nothing appears broken. Redo it from
// SETUP -> TOUCH CAL.
#define PANEL_BGR     1       // 1 = BGR colour order, 0 = RGB
#define PANEL_INVERT  0       // 0 = send INVOFF (this panel), 1 = INVON
#define PANEL_ROT     3       // 3 = landscape, USB to the left

#define SCR_W       320
#define SCR_H       240

#define SERIAL_ONLY   0       // 1 = headless, run without the display

// Draw all on-screen text in upper case.
//
// The 6x8 cell has no room below the baseline, so g j p q y are clipped and
// read as o i b a v. Rather than shrink the cap height to buy two descender
// rows - which would make everything harder to read at arm's length - the
// display is upper case throughout.
//
// This applies to captured strings too: SSIDs, vendor and model names. Those
// are case-sensitive evidence, so the SD LOG STILL RECORDS THEM EXACTLY AS
// RECEIVED - only the screen is folded. If you need an SSID's true case, read
// it from the CSV.
#define UI_ALL_CAPS   1

// Resistive touch calibration. Defaults are typical for this panel; the
// SETUP screen prints raw values so you can correct them.
#define TOUCH_RAW_X_MIN  300
#define TOUCH_RAW_X_MAX 3800
#define TOUCH_RAW_Y_MIN  300
#define TOUCH_RAW_Y_MAX 3800
#define TOUCH_Z_THRESH   350
#define TOUCH_SWAP_XY      1
#define TOUCH_INVERT_X     0
#define TOUCH_INVERT_Y     1

// ===========================================================================
// PROXIMITY GATE  -  the design centre
//
// Three tiers:
//   CONTACT  ~0.3 m  confirmation. Open the fixture.
//   NEAR     ~2.0 m  alert. Worth investigating.
//   ambient  beyond  logged silently, never alerts, but KEPT - the corridor
//                    vs room baseline diff depends on sub-threshold history.
//
// Budget at 2.4 GHz for a +17 dBm camera against the -35 dBm NEAR gate:
//   in room 2 m        17 - 46            = -29  pass
//   in room 4 m        17 - 52            = -35  borderline
//   next room 2 m      17 - 46 - 12(wall) = -41  reject
//   corridor 8 m       17 - 58 -  6(door) = -47  reject
// Interior hotel demise walls run 10-20 dB at 2.4 GHz, so distance gives
// ~6 dB of separation and the wall stacks 10-20 dB on top of that.
// ===========================================================================
#define THRESH_NEAR_24     (-35)
// CONTACT was -20 in an earlier revision. A +20 dBm camera 0.3 m behind a
// 10 dB wall reads -19.7 dBm and would have tripped the confirmation tier
// from the next room, so it is -15. Cost: in-room devices below about
// +13 dBm now only reach CONTACT at ~0.2 m rather than 0.3 m. Worth it for a
// tier whose whole value is that you can trust it.
#define THRESH_CONTACT_24  (-15)
#define THRESH_NEAR_5      (-42)   // 5 GHz loses ~7 dB more at the same range
#define THRESH_CONTACT_5   (-22)
// BLE raw fallback, used ONLY when an advert carries no transmit-power
// reference. No wall-proof claim is made for these: consumer BLE TX power
// spans about -20..+8 dBm, so a +8 dBm advertiser through a wall and a
// -4 dBm advertiser in the room genuinely overlap. The reference-based
// distance path below is the real answer for BLE.
#define THRESH_NEAR_BLE    (-55)
#define THRESH_CONTACT_BLE (-40)

#define TIER_HYSTERESIS_DB   4     // or the display flickers and reads broken
#define MIN_PKTS_FOR_NEAR    3     // one lucky reflection must not alert
#define RSSI_RING_LEN       32
#define PEAK_WINDOW_MS    2000
#define MEDIAN_WINDOW_MS  4000

// --- BLE ranging -----------------------------------------------------------
// Where an advert carries TX Power Level (AD 0x0A), an iBeacon measured
// power, or Eddystone ranging data, we compute path loss directly and remove
// transmit power as an error term.
#define BLE_1M_LOSS_DB      47     // radiated power -> RSSI at 1 m. Calibratable.
#define PATHLOSS_EXPONENT  2.0f    // n=2 free space; stays near 2 inside 2 m
#define DIST_NEAR_M        2.0f
#define DIST_CONTACT_M     0.30f

// ===========================================================================
// SCHEDULER
//
// Three hard constraints on this chip:
//   1. no simultaneous dual-band - esp_wifi_set_band_mode picks one
//   2. one antenna on WROOM-1, software time-division, no GPIO switching
//   3. WiFi and BLE also share time by coexistence
//
// So detection probability at a sweep position is radio-on dwell versus the
// target's transmit interval. BLE adverts range 20 ms to 10.24 s. A three-way
// round-robin at 33% duty needs 30+ s of wall clock per position to cover a
// 10 s advertiser - unusable while walking. Hence SEPARATE PASSES: two fast
// walks instead of one slow one. Round-robin is reserved for stationary WATCH.
// ===========================================================================
#define WIFI_BAND_SLICE_MS    1500
#define WIFI_CH_DWELL_MS       120   // beacons are ~100 ms
#define WATCH_SLICE_MS        4000
#define BAND_SWITCH_SETTLE_MS   30

static const uint8_t CH_24[] = {1, 6, 11};
// 5 GHz candidates. Which are actually settable is PROBED AT BOOT: the C5
// supports DFS channels but only passive radar detection, so the regulatory
// table may refuse 52-144 depending on country config. We measure instead of
// assuming, and report the answer on the SETUP screen.
static const uint8_t CH_5_CANDIDATES[] = {
    36, 40, 44, 48,
    52, 56, 60, 64,
    100,104,108,112,116,120,124,128,132,136,140,144,
    149,153,157,161,165
};

// Dwell accounting. No IMU on this board, so a new position is either marked
// by tapping the screen or inferred when the strongest peak moves.
#define AUTO_REMARK_DELTA_DB    6
#define SWEEP_WARN_BELOW_MS  1200

// ===========================================================================
// TRACK TABLE  (PSRAM - we have 8 MB, so nothing gets thrown away)
// ===========================================================================
#define MAX_TRACKS           600
#define NAME_LEN              32
#define SWEEP_PROFILE_SLOTS   24

// Streaming heuristic. A device inside the gate that is also pushing
// sustained uplink-dominant traffic is close to conclusive, and needs no
// vendor database at all - which is why it survives MAC randomisation.
#define STREAM_MIN_PKT_RATE       30
#define STREAM_MIN_UPLINK_RATIO  4.0f
#define STREAM_MIN_BYTES_TX   200000
#define STREAM_WINDOW_MS        3000

#define BASELINE_CAPTURE_MS    60000
#define ROOM_ONLY_DELTA_DB        12

#define SCORE_POSSIBLE  30
#define SCORE_LIKELY    60
#define SCORE_HIGH      85

#define LOG_DIR "/vulpecula"

// NVS namespace and keys. Declared here rather than beside the proximity
// code because the touch calibration, which appears earlier in the file,
// also persists to it.
static const char *NVS_NS  = "vulpecula";
static const char *NVS_KEY = "prox_cal";

// ---------------------------------------------------------------------------
// Constants gathered from the module files during the src/ split. Several
// are array bounds referenced by declarations in pure.h and vulpecula.h,
// so they have to be visible before those headers are read.
// ---------------------------------------------------------------------------

// from ble_cap.cpp
#define AD_APPEARANCE      0x19
#define AD_UUID16_SOME     0x02
#define AD_UUID16_ALL      0x03
#define AD_NAME_SHORT      0x08
#define AD_NAME_COMPLETE   0x09
#define AD_TX_POWER        0x0A
#define AD_SERVICE_DATA_16 0x16
#define AD_MFR_DATA        0xFF

// from calib.cpp
#define CAL_MIN_SAMPLES 40
#define CAL_MAX_SAMPLES 200

// from display.cpp
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_DIM     0x7BEF
#define C_GREEN   0x03E0
#define C_AMBER   0xFD20
#define C_RED     0xF800
#define C_CYAN    0x07FF
#define C_YELLOW  0xFFE0
#define C_PANEL   0x2104
#define C_BLUE    0x001F

// from importdb.cpp
#define OUI_DB_MAX      40000
#define NAMESIG_MAX        400
#define NS_ANY  0
#define NS_SSID 1
#define NS_BLE  2
#define OUI_BIN_MAGIC 0x564F5549UL     // "VOUI"
#define OUI_BIN_VER   1
#define RULES_MAX 200

// from parsers.cpp
#define IE_SSID        0x00
#define IE_VENDOR      0xDD
#define WPS_ATTR_DEV_NAME      0x1011
#define WPS_ATTR_MANUFACTURER  0x1021
#define WPS_ATTR_MODEL_NAME    0x1023
#define WPS_ATTR_PRIMARY_DEV   0x1054

// from sched.cpp
#define ALL_PASS_AUTO_MS 150000

// from touch.cpp
#define TCAL_INSET 28

// from triage.cpp
#define RISK_PROMOTE_MS  1500
#define RISK_DEMOTE_MS   6000

// from ui.cpp
#define TABBAR_Y 218
#define TAB_H     22
#define REDRAW_MS 200
#define LIST_ROWS  6
#define ROW_H     32
#define LIST_Y0   24          // first row's top edge
#define SCROLL_X  294         // left edge of the scroll column
#define SCROLL_W   26
#define ROW_CHARS  47         // row text width once the column is reserved
#define BAR_X 5
#define BAR_Y 74
#define BAR_W 310
#define BAR_H 18
#define LIST_SORT_MS 1000

// from vendor.cpp
#define WPS_CAT_COMPUTER   1
#define WPS_CAT_INPUT      2
#define WPS_CAT_PRINTER    3
#define WPS_CAT_CAMERA     4
#define WPS_CAT_STORAGE    5
#define WPS_CAT_NETWORK    6
#define WPS_CAT_DISPLAY    7
#define WPS_CAT_MULTIMEDIA 8
#define WPS_CAT_GAMING     9
#define WPS_CAT_TELEPHONE 10
#define WPS_CAT_AUDIO     11
#define APPEAR_PHONE        0x01
#define APPEAR_COMPUTER     0x02
#define APPEAR_WATCH        0x03
#define APPEAR_DISPLAY      0x05
#define APPEAR_REMOTE       0x06
#define APPEAR_TAG          0x08
#define APPEAR_MEDIA_PLAYER 0x0A
#define APPEAR_AUDIO_SINK   0x21
#define APPEAR_AUDIO_SOURCE 0x22
#define APPEAR_WEARABLE_AUD 0x25
#define APPEAR_AV_EQUIPMENT 0x27
#define APPEAR_DISPLAY_EQ   0x28
#define UUID_MICROPHONE_CONTROL 0x184D
#define UUID_AUDIO_INPUT_CTRL   0x1843

// from wifi_cap.cpp
#define CAP_QUEUE_LEN 96
#define BLE_QUEUE_LEN 64
#define IE_QUEUE_LEN   12
#define IE_BUF_LEN    320      // reaches the WPS IE in a typical beacon
#define TYPE_MGMT 0
#define TYPE_DATA 2
#define ST_PROBE_REQ  0x04
#define ST_PROBE_RESP 0x05
#define ST_BEACON     0x08

// ---------------------------------------------------------------------------
// CALIBRATION REFERENCE NORMALISATION
//
// The calibration measures RSSI from the reference beacon at 2.00 m and
// 0.30 m. Those readings are NOT the thresholds: they are readings of a
// specific transmitter. A threshold has to describe what a TYPICAL TARGET
// would produce at the same distance.
//
// Get this wrong and the error is silent. Calibrating against an +11 dBm
// reference while the gate is meant to catch a +17 dBm camera would set the
// threshold 6 dB too permissive, and the "2 metre" ring would quietly extend
// well past 2 metres - in the one component whose entire job is to be right
// about distance.
//
//   threshold = measured + (target_power - reference_power)
//
// REF values must match what your beacon actually transmits. The beacon
// prints the level the radio accepted on boot; if it differs from what was
// requested, put the ACCEPTED figure here.
//
// TARGET values are the design assumptions the shipped defaults were derived
// from: +17 dBm for a Wi-Fi camera, 0 dBm for a BLE device. Leave them alone
// unless you are deliberately tuning for a different class of device.
// ---------------------------------------------------------------------------
#define CAL_REF_WIFI_DBM      17    // what the reference beacon transmits
#define CAL_REF_BLE_DBM        0
#define CAL_TARGET_WIFI_DBM   17    // what a typical target transmits
#define CAL_TARGET_BLE_DBM     0

// The calibration locks onto the reference beacon BY NAME, not by signal
// rank. Ranking picked whichever track scored highest on the band, which
// could be a neighbour's access point - producing a plausible threshold
// measured against entirely the wrong transmitter. The beacon's name is
// deliberately distinctive; this is the other reason why.
#define CAL_REF_TAG "VULP-CAL"
