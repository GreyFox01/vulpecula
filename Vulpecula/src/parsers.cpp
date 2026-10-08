// parsers.cpp - All input parsing: 802.11 information elements, WPS attributes, BLE
// advertisement structures, and the CSV import files.
//
// PURE, and isolated on purpose. This is the only code in the project that
// processes data an attacker fully controls, so it depends on nothing from
// Arduino, is unit-tested directly, and is fuzzed by test/fuzz_parsers.cpp.
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

// ---------------------------------------------------------------------------
// The imported tables live here, with the code that reads them.
//
// They used to be defined in importdb.cpp, which fills them - but that is the
// hardware module, so the pure parser could not be linked without dragging SD
// and SPI in behind it. Keeping the data with its consumer is what lets
// test/fuzz_parsers.cpp link parsers.cpp on its own.
// ---------------------------------------------------------------------------
oui_rec_t *g_oui_db = NULL;
uint32_t   g_oui_n = 0;
namesig_t *g_namesig = NULL;
uint32_t   g_namesig_n = 0;
vrule_t  g_rules[RULES_MAX];
uint32_t g_rules_n = 0;

// ---------------------------------------------------------------------------
// Tagged-parameter walk
// ---------------------------------------------------------------------------

// WPS lives in a vendor-specific IE with the Wi-Fi Alliance OUI and type 4.
const uint8_t WPS_OUI_TYPE[4] = { 0x00, 0x50, 0xF2, 0x04 };


void copy_printable(char *dst, size_t cap, const uint8_t *src, uint16_t n)
{
    if (n > cap - 1) n = (uint16_t)(cap - 1);
    uint16_t k = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t c = src[i];
        if (c >= 0x20 && c <= 0x7E) dst[k++] = (char)c;
    }
    dst[k] = 0;
    // Trim trailing spaces - vendors pad these fields.
    while (k && dst[k-1] == ' ') dst[--k] = 0;
}

// Parse one WPS information element body (after the 4-byte OUI+type).
void parse_wps(track_t *t, const uint8_t *d, uint16_t dlen)
{
    uint16_t i = 0;
    while (i + 4 <= dlen) {
        uint16_t attr = (uint16_t)((d[i] << 8) | d[i+1]);
        uint16_t alen = (uint16_t)((d[i+2] << 8) | d[i+3]);
        if (i + 4 + alen > dlen) break;          // truncated, stop
        const uint8_t *v = &d[i+4];

        switch (attr) {
            case WPS_ATTR_MANUFACTURER:
                // An exact vendor string straight out of the beacon. Better
                // than any OUI table, and it costs one memcpy.
                if (alen && !t->vendor[0]) {
                    copy_printable(t->vendor, sizeof(t->vendor), v, alen);
                    if (t->vendor[0]) t->vendor_tier = DT_TIER_A;
                }
                break;

            case WPS_ATTR_MODEL_NAME:
                if (alen && !t->model[0])
                    copy_printable(t->model, sizeof(t->model), v, alen);
                break;

            case WPS_ATTR_DEV_NAME:
                // Only used as a name if nothing better arrived; the SSID is
                // usually more recognisable to the operator.
                if (alen && !t->name[0])
                    copy_printable(t->name, sizeof(t->name), v, alen);
                break;

            case WPS_ATTR_PRIMARY_DEV:
                // 2-byte category, 4-byte OUI, 2-byte subcategory. Only the
                // predefined Wi-Fi Alliance OUI is interpreted; a
                // vendor-specific OUI makes the subcategory meaningless.
                if (alen >= 8 && memcmp(v + 2, WPS_OUI_TYPE, 4) == 0) {
                    uint16_t cat = (uint16_t)((v[0] << 8) | v[1]);
                    uint16_t sub = (uint16_t)((v[6] << 8) | v[7]);
                    const char *detail = NULL;
                    dtype_t d2 = wps_category_to_dtype(cat, sub, &detail);
                    if (d2 != DTYPE_UNKNOWN) {
                        dtype_claim(t, d2, DT_TIER_A);
                        t->evidence |= E_WPS_DEVTYPE;
                        if (detail) t->dtype_detail = detail;
                    }
                }
                break;

            default:
                break;
        }
        i = (uint16_t)(i + 4 + alen);
    }
}

// Walk the tagged parameters of a beacon or probe response.
void parse_mgmt_ies(track_t *t, const uint8_t *f, uint16_t len)
{
    // 24-byte MAC header + 12-byte fixed beacon body, then tagged params.
    uint16_t i = 36;
    while (i + 2 <= len) {
        uint8_t id = f[i], elen = f[i+1];
        if (i + 2 + elen > len) break;
        const uint8_t *d = &f[i+2];

        if (id == IE_SSID) {
            if (elen == 0) {
                t->evidence |= E_HIDDEN_SSID;
            } else {
                char ssid[NAME_LEN];
                copy_printable(ssid, sizeof(ssid), d, elen);
                if (ssid[0]) { strncpy(t->name, ssid, NAME_LEN - 1); t->name[NAME_LEN-1] = 0; }
            }
        } else if (id == IE_VENDOR && elen >= 4 &&
                   memcmp(d, WPS_OUI_TYPE, 4) == 0) {
            parse_wps(t, d + 4, (uint16_t)(elen - 4));
        }
        i = (uint16_t)(i + 2 + elen);
    }
}


// ---------------------------------------------------------------------------
// BLE advertisement walk.
//
// Pure: takes a track and a raw payload, touches no hardware. Advertisements
// are attacker-controlled, so every length here is bounded against adv_len
// and this function is fuzzed by test/fuzz_parsers.cpp.
//
// Returns true if this advert supplied a transmit-power reference the track
// did not already have.
// ---------------------------------------------------------------------------
bool parse_ble_adv(track_t *t, const uint8_t *adv, uint8_t adv_len)
{
    bool had_ref = (t->ref.src != REF_NONE);
    size_t i = 0;
    while (i + 1 < adv_len) {
        uint8_t len = adv[i];
        if (len == 0) break;
        if (i + 1 + len > adv_len) break;       // truncated, stop
        uint8_t type = adv[i+1];
        const uint8_t *d = &adv[i+2];
        uint8_t dlen = (uint8_t)(len - 1);

        if (type == AD_NAME_COMPLETE || type == AD_NAME_SHORT) {
            // Never let a short name overwrite a complete one.
            if (!(t->name[0] && type == AD_NAME_SHORT)) {
                uint8_t cp = dlen < (NAME_LEN-1) ? dlen : (NAME_LEN-1);
                memcpy(t->name, d, cp); t->name[cp] = 0;
                for (uint8_t k = 0; k < cp; k++)
                    if (t->name[k] < 0x20 || t->name[k] > 0x7E) t->name[k] = '.';
            }
        } else if (type == AD_APPEARANCE) {
            // The device declaring its own category. Audio Source means a
            // microphone; Audio Sink means a speaker. Conflating the two
            // would put every Bluetooth speaker at the top of the list.
            if (dlen >= 2) {
                uint16_t ap = (uint16_t)(d[0] | (d[1] << 8));
                t->appearance = ap;
                dtype_t dt = appearance_to_dtype(ap);
                if (dt != DTYPE_UNKNOWN) {
                    dtype_claim(t, dt, DT_TIER_A);
                    t->evidence |= E_BLE_APPEARANCE;
                }
            }
        } else if (type == AD_TX_POWER) {
            if (dlen >= 1 && t->ref.src == REF_NONE)
                t->ref = prox_ref_from_txpower((int8_t)d[0]);
        } else if (type == AD_UUID16_SOME || type == AD_UUID16_ALL) {
            for (uint8_t k = 0; k + 1 < dlen && t->n_svc16 < 4; k += 2) {
                uint16_t u = (uint16_t)(d[k] | (d[k+1] << 8));
                bool dup = false;
                for (uint8_t m = 0; m < t->n_svc16; m++) if (t->svc16[m] == u) dup = true;
                if (!dup) t->svc16[t->n_svc16++] = u;
                // A SIG microphone or audio-input service means the device
                // has a microphone in it, declared by the device itself.
                dtype_t du = svc_uuid_to_dtype(u);
                if (du != DTYPE_UNKNOWN) dtype_claim(t, du, DT_TIER_A);
            }
        } else if (type == AD_SERVICE_DATA_16 && dlen >= 4) {
            uint16_t svc = (uint16_t)(d[0] | (d[1] << 8));
            if (svc == 0xFEAA && (d[2] == 0x00 || d[2] == 0x10)) {
                // Eddystone UID/URL: better reference than a bare TX power
                // field, so allow it to upgrade.
                if (t->ref.src == REF_NONE || t->ref.src == REF_ADV_TXPOWER)
                    t->ref = prox_ref_from_eddystone((int8_t)d[3]);
            }
        } else if (type == AD_MFR_DATA && dlen >= 2) {
            t->company_id = (uint16_t)(d[0] | (d[1] << 8));
            t->have_company = true;
            // iBeacon: company 0x004C, type 0x02, len 0x15, then 16-byte UUID
            // + major + minor + measured power. That measured power IS the
            // calibrated RSSI at 1 m, so it beats everything else.
            if (t->company_id == 0x004C && dlen >= 25 && d[2] == 0x02 && d[3] == 0x15)
                t->ref = prox_ref_from_ibeacon((int8_t)d[24]);
        }
        i += (size_t)len + 1;
    }

    return !had_ref && t->ref.src != REF_NONE;
}

// --- small helpers ---------------------------------------------------------
void str_lower(char *s)
{
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + 32);
}

dtype_t dtype_from_text(const char *s)
{
    if (!s) return DTYPE_UNKNOWN;
    if (!strcasecmp(s, "camera"))   return DTYPE_CAMERA;
    if (!strcasecmp(s, "mic"))      return DTYPE_MIC;
    if (!strcasecmp(s, "nvr"))      return DTYPE_NVR;
    if (!strcasecmp(s, "av"))       return DTYPE_AV;
    if (!strcasecmp(s, "speaker"))  return DTYPE_SPEAKER;
    if (!strcasecmp(s, "phone"))    return DTYPE_PHONE;
    if (!strcasecmp(s, "computer")) return DTYPE_COMPUTER;
    if (!strcasecmp(s, "network"))  return DTYPE_NETWORK;
    if (!strcasecmp(s, "display"))  return DTYPE_DISPLAY;
    if (!strcasecmp(s, "wearable")) return DTYPE_WEARABLE;
    if (!strcasecmp(s, "printer"))  return DTYPE_PRINTER;
    if (!strcasecmp(s, "tag"))      return DTYPE_TAG;
    return DTYPE_UNKNOWN;
}

uint8_t weight_from_text(const char *s)
{
    return (s && !strcasecmp(s, "strong")) ? SIG_STRONG : SIG_WEAK;
}

// Split a CSV line in place. Handles the quoting the IEEE registry uses -
// organisation names contain commas, so a naive split corrupts them and you
// end up with half a vendor name against the right prefix.
int csv_split(char *line, char **out, int maxf)
{
    int n = 0;
    char *p = line;
    while (n < maxf) {
        if (*p == '"') {
            p++;
            out[n++] = p;
            while (*p && !(*p == '"' && (p[1] == ',' || p[1] == 0 ||
                                         p[1] == '\r' || p[1] == '\n'))) {
                if (*p == '"' && p[1] == '"') p++;      // escaped quote
                p++;
            }
            if (*p == '"') *p++ = 0;
        } else {
            out[n++] = p;
            while (*p && *p != ',' && *p != '\r' && *p != '\n') p++;
        }
        if (*p == 0 || *p == '\r' || *p == '\n') { *p = 0; break; }
        *p++ = 0;
    }
    return n;
}

bool hex3(const char *s, uint8_t out[3])
{
    int k = 0;
    uint8_t cur = 0, nib = 0;
    for (const char *p = s; *p && k < 3; p++) {
        int v;
        if      (*p >= '0' && *p <= '9') v = *p - '0';
        else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
        else if (*p == ':' || *p == '-' || *p == '.') continue;
        else return false;
        cur = (uint8_t)((cur << 4) | v);
        if (++nib == 2) { out[k++] = cur; cur = 0; nib = 0; }
    }
    return k == 3;
}

// Apply the rules to one organisation name. First match wins, so order the
// file specific-before-generic.
void rules_apply(const char *vendor, uint8_t *dtype, uint8_t *weight)
{
    char low[VENDOR_NAME_LEN + 2];
    strncpy(low, vendor, sizeof(low) - 1);
    low[sizeof(low) - 1] = 0;
    str_lower(low);
    *dtype = DTYPE_UNKNOWN;
    *weight = SIG_WEAK;
    for (uint32_t i = 0; i < g_rules_n; i++)
        if (strstr(low, g_rules[i].kw)) {
            *dtype  = g_rules[i].dtype;
            *weight = g_rules[i].weight;
            return;
        }
}

// Binary search. Called from the pump task for tracks that matter, never for
// every ambient advert - see the bounded-lookup rule in classify_track.
const oui_rec_t *oui_db_lookup(const uint8_t mac[6])
{
    if (!g_oui_db || !g_oui_n) return NULL;
    if (mac[0] & 0x02) return NULL;        // locally administered: no vendor
    uint32_t lo = 0, hi = g_oui_n - 1;
    while (lo <= hi) {
        uint32_t mid = (lo + hi) / 2;
        int c = memcmp(mac, g_oui_db[mid].oui, 3);
        if (c == 0) return &g_oui_db[mid];
        if (c < 0) { if (!mid) break; hi = mid - 1; }
        else lo = mid + 1;
    }
    return NULL;
}

// Runtime name-pattern match against the imported patterns.
bool namesig_match(const char *name, bool is_ble,
                          uint8_t *dtype, uint8_t *weight)
{
    if (!g_namesig || !g_namesig_n || !name || !name[0]) return false;
    char low[NAME_LEN + 2];
    strncpy(low, name, sizeof(low) - 1);
    low[sizeof(low) - 1] = 0;
    str_lower(low);

    bool found = false;
    for (uint32_t i = 0; i < g_namesig_n; i++) {
        const namesig_t *s = &g_namesig[i];
        if (s->scope == NS_SSID && is_ble) continue;
        if (s->scope == NS_BLE && !is_ble) continue;
        if (!strstr(low, s->pat)) continue;
        // Prefer a STRONG match over a WEAK one rather than taking the first.
        if (!found || s->weight > *weight) { *dtype = s->dtype; *weight = s->weight; }
        found = true;
        if (*weight == SIG_STRONG) break;
    }
    return found;
}

// --- OUI database ----------------------------------------------------------
int cmp_oui(const void *a, const void *b)
{
    return memcmp(((const oui_rec_t *)a)->oui, ((const oui_rec_t *)b)->oui, 3);
}
