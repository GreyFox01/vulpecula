// types.h - every struct, enum and typedef.
//
// These used to be crowded into one block at the top of the .ino because the
// Arduino IDE injects function prototypes ahead of whatever it finds first,
// and a prototype naming a later-declared type fails to parse. Code in src/
// is NOT preprocessed that way, so this is now an ordinary header and
// declaration order is ordinary C++.
#pragma once
#include "config.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ===========================================================================
// TYPE DEFINITIONS
//
// DO NOT MOVE THESE BACK DOWN NEXT TO THE CODE THAT USES THEM.
//
// The Arduino IDE preprocessor generates a forward declaration for every
// function in a .ino and injects the whole set at the FIRST function
// definition it finds. Any injected prototype that mentions a type declared
// later in the file then fails to parse, and the errors cascade into dozens
// of misleading messages like
//
//     error: variable or field 'ring_reset' declared void
//     error: 'tier_name' redeclared as different kind of entity
//
// pointing at code that is perfectly correct. Keeping every struct, enum and
// typedef above the first function definition is what makes a single-file
// .ino of this size compile at all. This is also why plain g++ builds of this
// file succeed even when the IDE fails: g++ does no prototype injection.
// ===========================================================================

typedef enum { BAND_24 = 0, BAND_5, BAND_BLE, BAND_COUNT } rband_t;
typedef enum { TIER_AMBIENT = 0, TIER_NEAR, TIER_CONTACT } tier_t;

typedef struct { int8_t rssi; uint32_t ts; } rssi_sample_t;
typedef struct {
    rssi_sample_t s[RSSI_RING_LEN];
    uint8_t head, count;
} rssi_ring_t;

typedef enum {
    REF_NONE = 0,
    REF_ADV_TXPOWER,   // AD type 0x0A
    REF_IBEACON,       // calibrated RSSI at 1 m - the best case
    REF_EDDYSTONE      // ranging power at 0 m
} ref_source_t;

typedef struct { ref_source_t src; int8_t rssi_at_1m; } ranging_ref_t;

typedef struct {
    int8_t near_thresh[BAND_COUNT];
    int8_t contact_thresh[BAND_COUNT];
    int8_t ble_1m_loss;
    float  pathloss_n;
    bool   calibrated[BAND_COUNT];
    // Set where a threshold was DERIVED from another band's measured offset
    // rather than measured directly - see cal_derive_5g(). Kept separate so
    // the SETUP screen never presents a derived value as a measured one.
    bool   derived[BAND_COUNT];
} prox_cal_t;

typedef enum { PHASE_FREE = 0, PHASE_CORRIDOR, PHASE_ROOM, PHASE_COUNT } phase_t;

enum {
    E_SOFTAP_CAM_SSID = 1u << 0,
    E_CAM_OUI_STRONG  = 1u << 1,
    E_CAM_OUI_WEAK    = 1u << 2,
    E_STREAM_PROFILE  = 1u << 3,
    E_CONTACT_TIER    = 1u << 4,
    E_ROOM_ONLY       = 1u << 5,
    E_WILDCARD_PROBE  = 1u << 6,
    E_HIDDEN_SSID     = 1u << 7,
    E_BLE_CAM_NAME    = 1u << 8,
    E_BLE_MIC_NAME    = 1u << 9,
    E_BLE_SVC_UUID    = 1u << 10,
    E_BLE_COMPANY     = 1u << 11,
    E_SHARP_PEAK      = 1u << 12,
    E_BROAD_PEAK      = 1u << 13,
    E_WPS_DEVTYPE     = 1u << 14,   // device declared its own type in WPS
    E_BLE_APPEARANCE  = 1u << 15,   // device declared its own BLE appearance
};

// Device type, and how strongly it is known. Tier order is the confidence
// order, so a numerically LOWER tier always wins in dtype_claim().
typedef enum {
    DTYPE_UNKNOWN = 0, DTYPE_CAMERA, DTYPE_MIC, DTYPE_NVR, DTYPE_AV,
    DTYPE_SPEAKER, DTYPE_PHONE, DTYPE_COMPUTER, DTYPE_NETWORK,
    DTYPE_DISPLAY, DTYPE_WEARABLE, DTYPE_PRINTER, DTYPE_TAG
} dtype_t;

#define DT_TIER_A 0   // self-declared: WPS device type, BLE appearance/UUID
#define DT_TIER_B 1   // behaviour: sustained uplink-dominant traffic
#define DT_TIER_C 2   // advertised name or SSID pattern
#define DT_TIER_D 3   // OUI belongs to a camera vendor
#define DT_TIER_NONE 9

// Triage level.
typedef enum { RISK_LOW = 0, RISK_MEDIUM, RISK_HIGH, RISK_CRITICAL } risk_t;

typedef struct {
    bool     used;
    uint8_t  mac[6];
    uint8_t  addr_type;
    rband_t  band;
    uint8_t  channel;

    rssi_ring_t   ring;
    ranging_ref_t ref;
    tier_t   tier, tier_best;
    int8_t   peak_session;
    float    dist_m;

    uint32_t first_ms, last_ms, pkts;

    uint32_t n_beacon, n_probe_req, n_probe_resp, n_mgmt_other;
    uint32_t n_data_up, n_data_down;
    uint32_t bytes_up, bytes_down;
    uint32_t rate_win_start, rate_win_pkts, pkt_rate;
    bool     is_ap;

    char     name[NAME_LEN];
    uint16_t company_id;
    bool     have_company;
    uint16_t svc16[4];
    uint8_t  n_svc16;

    int8_t   peak_phase[PHASE_COUNT];
    uint8_t  phase_mask;

    int8_t   profile[SWEEP_PROFILE_SLOTS];
    uint8_t  profile_head, profile_count;

    uint32_t evidence;
    uint8_t  score;
    bool     alerted;

    // --- identity and type ---
    char     vendor[24];     // from WPS Manufacturer, else the curated OUI table
    char     model[20];      // from WPS Model Name
    uint8_t  vendor_tier;
    uint16_t appearance;     // raw BLE Appearance, 0 if never advertised
    dtype_t  dtype;
    uint8_t  dtype_tier;
    const char *dtype_detail;   // e.g. "security camera", from WPS subcategory

    // --- triage ---
    risk_t   risk, risk_raw;
    uint32_t risk_since;
    const char *risk_why;
} track_t;

typedef enum { SIG_WEAK = 0, SIG_STRONG } sig_weight_t;
typedef struct { const char *pat; sig_weight_t w; } str_sig_t;
typedef struct { uint8_t oui[3]; sig_weight_t w; const char *vendor; } oui_sig_t;

typedef enum { CONF_LOW = 0, CONF_POSSIBLE, CONF_LIKELY, CONF_HIGH } confidence_t;

typedef struct {
    uint8_t  hdr[36];       // enough for a 4-address header plus QoS
    uint16_t len;
    int8_t   rssi;
    uint8_t  channel, band;
} cap_item_t;

typedef struct {
    uint8_t mac[6], addr_type;
    int8_t  rssi;
    uint8_t adv_len, adv[62];
} ble_item_t;

typedef enum { MODE_IDLE = 0, MODE_WIFI_SWEEP, MODE_BLE_SWEEP,
               MODE_SWEEP_ALL, MODE_WATCH } sweep_mode_t;

typedef enum { CAL_IDLE = 0, CAL_NEAR, CAL_CONTACT, CAL_DONE, CAL_FAILED } cal_state_t;

// One curated OUI entry. dtype is a hint recorded at TIER D only.
typedef struct {
    uint8_t      oui[3];
    const char  *vendor;
    dtype_t      dtype;
    sig_weight_t w;
} vendor_sig_t;

// A self-erasing, self-diffing text field on the display. The flicker in the
// first build came from clearing a region and repainting it 4.5 times a
// second; a field instead pads to a fixed width and rewrites only the
// character cells whose content actually changed, so a steady value costs no
// SPI traffic at all.
typedef struct {
    int16_t  x, y;
    uint8_t  w;          // width in characters
    uint8_t  big;        // 0 = 6x8, 1 = 12x16
    uint16_t fg, bg;
    char     cur[44];
} field_t;

// Touch calibration, measured by the guided routine and persisted to NVS.
typedef struct {
    uint16_t x_min, x_max;   // raw range along the screen's X axis
    uint16_t y_min, y_max;   // raw range along the screen's Y axis
    bool     swap;           // controller axes transposed vs the display
    bool     inv_x, inv_y;
    bool     valid;
} touch_cal_t;

// UI screens. SCR_TAB_COUNT marks the end of the tab bar: the procedure
// pages after it are reached only from SETUP, and deliberately have no tab so
// a stray tap cannot abandon a half-explained procedure.
typedef enum { SCR_WELCOME = 0, SCR_SWEEP, SCR_LIST, SCR_DETAIL, SCR_SETUP,
               SCR_TAB_COUNT,
               SCR_BASELINE_INFO, SCR_RSSICAL_INFO, SCR_WIPE_INFO,
               SCR_COUNT } screen_t;

// A procedure explainer page.
#define INFO_MAX_LINES 16
typedef struct {
    const char *title;
    const char *lines[INFO_MAX_LINES];
} infopage_t;

// A touch target.
typedef struct { int16_t x, y, w, h; } rect_t;

// A horizontal bar that paints only the segment that changed.
typedef struct { int16_t x, y, w, h; int16_t cur; uint16_t col; } bar_t;

// --- imported characterisation data (see IMPORTED VENDOR AND SIGNATURE
//     DATABASE further down for what the files are and why they are split) ---

// One imported vendor record. 32 bytes exactly, so the binary cache on the SD
// card is a flat sorted array and a record index is a multiplication.
#define VENDOR_NAME_LEN 26
typedef struct {
    uint8_t oui[3];
    uint8_t dtype;                     // dtype_t
    uint8_t weight;                    // sig_weight_t
    char    vendor[VENDOR_NAME_LEN];
    uint8_t pad;
} oui_rec_t;

// An imported runtime pattern, matched against SSIDs and BLE names.
#define NAMESIG_PAT_LEN 24
typedef struct {
    char    pat[NAMESIG_PAT_LEN];
    uint8_t scope;                     // NS_ANY / NS_SSID / NS_BLE
    uint8_t dtype;
    uint8_t weight;
} namesig_t;

typedef struct { uint32_t magic, version, count, reserved; } oui_bin_hdr_t;

// One judgement rule: an organisation-name keyword mapped to a device type.


// ---------------------------------------------------------------------------

// One judgement rule from rules.csv: an organisation-name keyword mapped to a
// device type and a confidence weight.
typedef struct { char kw[28]; uint8_t dtype, weight; } vrule_t;

// Panel colour order, chosen at runtime rather than compiled in.
//
// A red/blue swap leaves the green bits alone, so ambient stays green while
// CONTACT renders blue and NEAR renders teal: the display looks plausible
// while both alert states are wrong. That is not something to leave to a
// #define nobody verifies, so the operator confirms it once and the answer
// is persisted.
typedef struct {
    bool bgr;          // true = BGR order
    bool confirmed;    // the operator has actually looked at the bars
} disp_cfg_t;
