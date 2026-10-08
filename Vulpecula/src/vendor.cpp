// vendor.cpp - Device type, vendor tables and the WPS/BLE type mappings. PURE.
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
// SIGNATURES  -  the field-editable part
//
// PROVENANCE WARNING. Read before trusting any match.
//
// Hidden cameras are usually made with no brand at all, or under random names
// absent from the IEEE OUI registry. So OUI matching has poor coverage on
// exactly the devices this tool is for, and the SoC-vendor prefixes that DO
// appear (Realtek, Anyka, Ingenic, Espressif) are shared with thousands of
// innocent devices - including this board itself.
//
// Entries are therefore weighted by how much they actually prove:
//   STRONG  a vendor that only makes cameras, or a product-specific name
//   WEAK    a camera-common SoC vendor or generic keyword. Logged, scored
//           low, never alerts on its own.
//
// The real discriminators are NOT in this table. They are the proximity gate,
// the corridor/room baseline diff and the streaming-traffic heuristic - none
// of which need a database, and all of which survive MAC randomisation.
//
// Treat everything here as a lead requiring field confirmation.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

// MINIMAL FALLBACK ONLY.
//
// These used to be the primary signature source and are now a skeleton for a
// board with no SD card in it. The real tables are names.csv and rules.csv on
// the card: editable without a reflash, and matched by exactly one code path
// (namesig_match) instead of two that could disagree.
//
// Do not grow these. Add to names.csv instead.
const str_sig_t SIG_SSID[] = {
    {"ipcam",   SIG_STRONG}, {"ipc-",    SIG_STRONG},
    {"netcam",  SIG_STRONG}, {"wificam", SIG_STRONG},
};
const uint32_t N_SIG_SSID = sizeof(SIG_SSID) / sizeof(SIG_SSID[0]);

const str_sig_t SIG_BLE_CAM[] = {
    {"gopro",   SIG_STRONG}, {"spycam",  SIG_STRONG},
};
const uint32_t N_SIG_BLE_CAM = sizeof(SIG_BLE_CAM) / sizeof(SIG_BLE_CAM[0]);

const str_sig_t SIG_BLE_MIC[] = {
    {"dji mic", SIG_STRONG}, {"wireless mic", SIG_STRONG},
};
const uint32_t N_SIG_BLE_MIC = sizeof(SIG_BLE_MIC) / sizeof(SIG_BLE_MIC[0]);

// BLE 16-bit service UUIDs worth noticing. 0xFEAA is Eddystone, which also
// gives us a ranging reference; 0xFE59 is Nordic DFU, common on cheap cameras
// left in update mode.
const uint16_t SIG_SVC16[] = { 0xFEAA, 0xFE59, 0xFDF0 };
const uint32_t N_SIG_SVC16 = sizeof(SIG_SVC16) / sizeof(SIG_SVC16[0]);

// SIG company IDs seen on surveillance accessories. Verify locally before
// leaning on either.
const uint16_t SIG_COMPANY[] = { 0x0B84, 0x09C8 };
const uint32_t N_SIG_COMPANY = sizeof(SIG_COMPANY) / sizeof(SIG_COMPANY[0]);

bool istrstr(const char *hay, const char *needle)
{
    if (!hay || !needle || !*needle) return false;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return true;
    }
    return false;
}

// Scan the whole table rather than returning on first hit, so a STRONG match
// always wins over an incidental WEAK keyword in the same name.
bool sig_match_str(const str_sig_t *tbl, uint32_t n, const char *hay,
                          sig_weight_t *w_out)
{
    if (!hay || !hay[0]) return false;
    bool found = false; sig_weight_t best = SIG_WEAK;
    for (uint32_t i = 0; i < n; i++) {
        if (!istrstr(hay, tbl[i].pat)) continue;
        if (!found || tbl[i].w > best) best = tbl[i].w;
        found = true;
        if (best == SIG_STRONG) break;
    }
    if (found && w_out) *w_out = best;
    return found;
}

// sig_match_oui() was replaced by vendor_lookup() in the DEVICE TYPE AND
// VENDOR section below, which returns the vendor string and a device-type
// hint from one table instead of just a weight.

// ===========================================================================
// DEVICE TYPE AND VENDOR
//
// Device type comes from four tiers of evidence, and the tier is carried
// alongside the type because it decides how much the triage stage trusts it:
//
//   TIER A  self-declared in a standards field. The device says what it is,
//           in cleartext, in its own beacon or advertisement:
//             - WPS Primary Device Type in a Wi-Fi beacon
//             - BLE Appearance, or a SIG audio/microphone service UUID
//           This is the strongest signal in the whole firmware.
//   TIER B  behaviour. Sustained uplink-dominant traffic = camera-like.
//           Needs no database and survives MAC randomisation.
//   TIER C  advertised name or SSID matches a pattern.
//   TIER D  the OUI belongs to a vendor that makes cameras. Weakest: plenty
//           of no-name cameras use SoC-vendor prefixes shared with masses of
//           innocent hardware.
//
// VENDOR STRINGS come from two places, and the good one is not the database.
// A Wi-Fi device in SoftAP mode usually broadcasts WPS Manufacturer (0x1021)
// and Model Name (0x1023) attributes as plain strings in its beacon. That is
// exact, needs no lookup, and beats an OUI table outright. The curated OUI
// table below is only the fallback for devices that publish nothing.
// ===========================================================================

const char *dtype_name(dtype_t d)
{
    switch (d) {
        case DTYPE_CAMERA:   return "CAMERA";
        case DTYPE_MIC:      return "MIC";
        case DTYPE_AV:       return "AV GEAR";
        case DTYPE_NVR:      return "NVR/DVR";
        case DTYPE_PHONE:    return "PHONE";
        case DTYPE_COMPUTER: return "COMPUTER";
        case DTYPE_NETWORK:  return "NETWORK";
        case DTYPE_DISPLAY:  return "DISPLAY";
        case DTYPE_SPEAKER:  return "SPEAKER";
        case DTYPE_WEARABLE: return "WEARABLE";
        case DTYPE_PRINTER:  return "PRINTER";
        case DTYPE_TAG:      return "TAG";
        default:             return "UNKNOWN";
    }
}

const char *dtier_name(uint8_t t)
{
    switch (t) {
        case DT_TIER_A: return "declared";
        case DT_TIER_B: return "behaviour";
        case DT_TIER_C: return "name";
        case DT_TIER_D: return "vendor";
        default:        return "none";
    }
}

// A camera or a microphone is what this tool exists to find. Everything else
// is context, however interesting.
bool dtype_is_recorder(dtype_t d)
{
    return d == DTYPE_CAMERA || d == DTYPE_MIC || d == DTYPE_NVR;
}

// ---------------------------------------------------------------------------
// CURATED OUI TABLE
//
// PROVENANCE. Every entry below is hand-checked. It is deliberately SHORT
// rather than padded out to a few hundred guesses: a wrong prefix does not
// fail quietly, it puts a confident vendor name and device type next to
// somebody's phone, and a triage label that is wrong once is worse than no
// label at all.
//
// To extend it accurately, run tools/make_vendor_table.py against a freshly
// downloaded IEEE registry. It filters the real registry by vendor keyword
// and emits entries in this exact format, so the table stays correct and
// current without anyone typing prefixes from memory.
//
// dtype here is a HINT ONLY and is recorded as TIER D. It never sets the
// device type when a WPS or BLE declaration is available.
// ---------------------------------------------------------------------------
const vendor_sig_t VENDOR_OUI[] = {
    // --- surveillance vendors: these make cameras and NVRs, little else ---
    { {0x00,0x40,0x48}, "Hikvision",      DTYPE_CAMERA,  SIG_STRONG },
    { {0x44,0x19,0xB6}, "Hikvision",      DTYPE_CAMERA,  SIG_STRONG },
    { {0xC0,0x56,0xE3}, "Hikvision",      DTYPE_CAMERA,  SIG_STRONG },
    { {0x4C,0x11,0xBF}, "Dahua",          DTYPE_CAMERA,  SIG_STRONG },
    { {0x90,0x02,0xA9}, "Dahua",          DTYPE_CAMERA,  SIG_STRONG },
    { {0x3C,0xEF,0x8C}, "Dahua",          DTYPE_CAMERA,  SIG_STRONG },
    { {0xAC,0xCC,0x8E}, "Axis",           DTYPE_CAMERA,  SIG_STRONG },
    { {0x00,0x40,0x8C}, "Axis",           DTYPE_CAMERA,  SIG_STRONG },
    { {0xE0,0x50,0x8B}, "Uniview",        DTYPE_CAMERA,  SIG_STRONG },
    { {0x48,0xEA,0x63}, "Uniview",        DTYPE_CAMERA,  SIG_STRONG },
    { {0x00,0x0F,0x7C}, "ACTi",           DTYPE_CAMERA,  SIG_STRONG },
    { {0x00,0x02,0xD1}, "Vivotek",        DTYPE_CAMERA,  SIG_STRONG },
    { {0x00,0x1A,0x07}, "Arecont Vision", DTYPE_CAMERA,  SIG_STRONG },

    // --- SoC and module vendors found inside no-name cameras.
    //     Shared with thousands of unrelated devices, so WEAK and typed
    //     UNKNOWN: presence is a lead, not an identification. ---
    { {0x8C,0xCE,0x4E}, "Anyka",          DTYPE_UNKNOWN, SIG_WEAK },
    { {0x00,0x25,0x9E}, "Ingenic",        DTYPE_UNKNOWN, SIG_WEAK },
    { {0x00,0xE0,0x4C}, "Realtek",        DTYPE_UNKNOWN, SIG_WEAK },
    { {0x1C,0xBF,0xCE}, "Shenzhen OEM",   DTYPE_UNKNOWN, SIG_WEAK },
    { {0x18,0xFE,0x34}, "Espressif",      DTYPE_UNKNOWN, SIG_WEAK },
    { {0x7C,0xDF,0xA1}, "Espressif",      DTYPE_UNKNOWN, SIG_WEAK },
};
#define N_VENDOR_OUI (sizeof(VENDOR_OUI)/sizeof(VENDOR_OUI[0]))

const vendor_sig_t *vendor_lookup(const uint8_t mac[6])
{
    // A locally-administered address carries no vendor information at all
    // (bit 1 of octet 0), and plenty of cameras randomise. Matching one
    // against a vendor table produces confident nonsense, so skip it.
    if (mac[0] & 0x02) return NULL;
    for (uint32_t i = 0; i < N_VENDOR_OUI; i++)
        if (memcmp(mac, VENDOR_OUI[i].oui, 3) == 0) return &VENDOR_OUI[i];
    return NULL;
}

// Record a device type only if the new evidence is at least as strong as what
// we already had. Stops a name keyword from overwriting a WPS declaration.
void dtype_claim(track_t *t, dtype_t d, uint8_t tier)
{
    if (d == DTYPE_UNKNOWN) return;
    if (t->dtype != DTYPE_UNKNOWN && t->dtype_tier <= tier) return;  // A < D
    t->dtype = d;
    t->dtype_tier = tier;
}

// ---------------------------------------------------------------------------
// WPS Primary Device Type -> our device type.
//
// The attribute value is 8 bytes: 2-byte CategoryID, 4-byte OUI
// (00 50 F2 04 for the predefined categories), 2-byte SubcategoryID.
// Camera is category 4. WSC 1.0 defined only subcategory 1, Digital Still
// Camera; WSC 2.0 adds video, web and security camera, and an Audio Devices
// category. The subcategory numbers below are best-effort and should be
// checked against a live capture - but the CATEGORY is what drives typing
// here, so an unrecognised subcategory still yields the right answer.
// ---------------------------------------------------------------------------

dtype_t wps_category_to_dtype(uint16_t cat, uint16_t sub, const char **detail)
{
    *detail = NULL;
    switch (cat) {
        case WPS_CAT_CAMERA:
            switch (sub) {
                case 1: *detail = "still camera";    break;
                case 2: *detail = "video camera";    break;
                case 3: *detail = "web camera";      break;
                case 4: *detail = "security camera"; break;
                default: *detail = "camera";         break;
            }
            return DTYPE_CAMERA;
        case WPS_CAT_AUDIO:      *detail = "audio device"; return DTYPE_MIC;
        case WPS_CAT_MULTIMEDIA: *detail = "multimedia";   return DTYPE_AV;
        case WPS_CAT_DISPLAY:    return DTYPE_DISPLAY;
        case WPS_CAT_NETWORK:    return DTYPE_NETWORK;
        case WPS_CAT_COMPUTER:   return DTYPE_COMPUTER;
        case WPS_CAT_TELEPHONE:  return DTYPE_PHONE;
        case WPS_CAT_PRINTER:    return DTYPE_PRINTER;
        default:                 return DTYPE_UNKNOWN;
    }
}

// ---------------------------------------------------------------------------
// BLE Appearance -> our device type.
//
// Appearance is 16 bits: bits 15..6 category, bits 5..0 subcategory. The
// categories below are from the SIG assigned numbers; verify against a
// capture before relying on the exact values. Audio Source is the notable
// one - a wireless microphone is an audio SOURCE, while a speaker is a sink,
// so the two must not be conflated.
// ---------------------------------------------------------------------------

dtype_t appearance_to_dtype(uint16_t appear)
{
    switch (appear >> 6) {
        case APPEAR_AUDIO_SOURCE: return DTYPE_MIC;      // mic, not speaker
        case APPEAR_WEARABLE_AUD: return DTYPE_MIC;
        case APPEAR_AV_EQUIPMENT: return DTYPE_AV;
        case APPEAR_MEDIA_PLAYER: return DTYPE_AV;
        case APPEAR_AUDIO_SINK:   return DTYPE_SPEAKER;
        case APPEAR_PHONE:        return DTYPE_PHONE;
        case APPEAR_COMPUTER:     return DTYPE_COMPUTER;
        case APPEAR_WATCH:        return DTYPE_WEARABLE;
        case APPEAR_TAG:          return DTYPE_TAG;
        case APPEAR_DISPLAY:
        case APPEAR_DISPLAY_EQ:   return DTYPE_DISPLAY;
        case APPEAR_REMOTE:       return DTYPE_UNKNOWN;
        default:                  return DTYPE_UNKNOWN;
    }
}

// SIG service UUIDs that imply audio capture. 0x184D Microphone Control and
// 0x1843 Audio Input Control are the two that actually mean "this thing has a
// microphone in it". Verify against a capture; treat as TIER A when present.

dtype_t svc_uuid_to_dtype(uint16_t u)
{
    if (u == UUID_MICROPHONE_CONTROL || u == UUID_AUDIO_INPUT_CTRL) return DTYPE_MIC;
    return DTYPE_UNKNOWN;
}
