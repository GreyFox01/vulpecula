// classify.cpp - Evidence weighting and the streaming heuristic. PURE.
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
// CLASSIFIER
//
// Two rules that matter:
//  1. Scoring runs on every track, but ALERTS only fire on NEAR or better.
//     A perfect signature match 20 m away in the next room is not this tool's
//     problem, and surfacing it is how a sweep tool becomes unreadable.
//  2. No single field reaches the HIGH band alone. Confidence comes from a
//     proximity tier plus independent corroboration.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

int score_for(uint32_t bit)
{
    switch (bit) {
        case E_SOFTAP_CAM_SSID: return 40;
        case E_CAM_OUI_STRONG:  return 25;
        case E_CAM_OUI_WEAK:    return  5;
        case E_STREAM_PROFILE:  return 40;
        case E_CONTACT_TIER:    return 25;
        case E_ROOM_ONLY:       return 30;
        case E_WILDCARD_PROBE:  return 10;
        case E_HIDDEN_SSID:     return 10;
        case E_BLE_CAM_NAME:    return 35;
        case E_BLE_MIC_NAME:    return 35;
        case E_BLE_SVC_UUID:    return 20;
        case E_BLE_COMPANY:     return 10;
        case E_SHARP_PEAK:      return  0;   // shape alone is not evidence
        case E_BROAD_PEAK:      return 10;   // breadth weakly favours in-room
        default:                return  0;
    }
}

// The streaming heuristic. Strongest signal available for a camera that has
// joined the room's own WiFi, where there are no beacons to catch and the MAC
// may be randomised. Excludes APs deliberately: an access point is
// uplink-dominant from our vantage point too, and the hotel's own AP is not
// what we are hunting.
bool looks_like_streaming(const track_t *t)
{
    if (t->is_ap) return false;
    if (t->bytes_up < STREAM_MIN_BYTES_TX) return false;
    if (t->pkt_rate < STREAM_MIN_PKT_RATE) return false;
    float ratio = (t->bytes_down > 0)
                ? (float)t->bytes_up / (float)t->bytes_down
                : STREAM_MIN_UPLINK_RATIO + 1.0f;
    return ratio >= STREAM_MIN_UPLINK_RATIO;
}

void classify_track(track_t *t)
{
    if (!t) return;

    if (t->band == BAND_24 || t->band == BAND_5) {
        // SSID pattern matching lives in the imported names.csv path below,
        // not here. It used to be done twice against two different tables.
        if (looks_like_streaming(t)) {
            t->evidence |= E_STREAM_PROFILE;
            // Sustained uplink-dominant traffic from a non-AP is camera-like
            // behaviour. TIER B: no database, survives MAC randomisation.
            dtype_claim(t, DTYPE_CAMERA, DT_TIER_B);
        } else {
            t->evidence &= ~(uint32_t)E_STREAM_PROFILE;
        }
    }

    // Vendor and device type from the OUI, imported database first and the
    // compiled table only as a fallback for a board with no card in it.
    // Either way this is TIER D, so it can never override a device that
    // declared its own type via WPS or BLE.
    //
    // Bounded on purpose: a hotel puts 300 ambient devices in the table and
    // resolving all of them would waste the whole budget on things that can
    // never alert. Only tracks that crossed the gate, or that already carry
    // some evidence, get looked up.
    if (!t->vendor[0] && (t->tier_best >= TIER_NEAR || t->evidence)) {
        const oui_rec_t *ir = oui_db_lookup(t->mac);
        if (ir) {
            t->evidence |= (ir->weight == SIG_STRONG) ? E_CAM_OUI_STRONG
                                                      : E_CAM_OUI_WEAK;
            strncpy(t->vendor, ir->vendor, sizeof(t->vendor) - 1);
            t->vendor[sizeof(t->vendor) - 1] = 0;
            t->vendor_tier = DT_TIER_D;
            dtype_claim(t, (dtype_t)ir->dtype, DT_TIER_D);
        } else {
            const vendor_sig_t *v = vendor_lookup(t->mac);
            if (v) {
                t->evidence |= (v->w == SIG_STRONG) ? E_CAM_OUI_STRONG
                                                    : E_CAM_OUI_WEAK;
                strncpy(t->vendor, v->vendor, sizeof(t->vendor) - 1);
                t->vendor[sizeof(t->vendor) - 1] = 0;
                t->vendor_tier = DT_TIER_D;
                dtype_claim(t, v->dtype, DT_TIER_D);
            }
        }
    }

    // Imported runtime name patterns. These cover the no-name cameras whose
    // MAC prefix says nothing useful, which is most of them.
    if (t->name[0]) {
        uint8_t nd = DTYPE_UNKNOWN, nw = SIG_WEAK;
        if (namesig_match(t->name, t->band == BAND_BLE, &nd, &nw)) {
            if (nw == SIG_STRONG) {
                if (t->band == BAND_BLE) {
                    t->evidence |= (nd == DTYPE_MIC) ? E_BLE_MIC_NAME
                                                     : E_BLE_CAM_NAME;
                } else {
                    t->evidence |= E_SOFTAP_CAM_SSID;
                }
            }
            dtype_claim(t, (dtype_t)nd, DT_TIER_C);
        }
    }

    if (t->band == BAND_BLE) {
        // BLE name matching: likewise handled once, by namesig_match below.
        for (uint8_t i = 0; i < t->n_svc16; i++)
            for (uint32_t k = 0; k < N_SIG_SVC16; k++)
                if (t->svc16[i] == SIG_SVC16[k]) t->evidence |= E_BLE_SVC_UUID;
        if (t->have_company)
            for (uint32_t k = 0; k < N_SIG_COMPANY; k++)
                if (t->company_id == SIG_COMPANY[k]) t->evidence |= E_BLE_COMPANY;
    }

    int total = 0;
    for (uint8_t b = 0; b < 32; b++) {
        uint32_t bit = 1u << b;
        if (t->evidence & bit) total += score_for(bit);
    }

    // Proximity as a MULTIPLIER, not an additive term. An out-of-room device
    // with a perfect signature match must not climb the list and crowd out
    // the thing sitting in the smoke detector.
    if (t->tier_best == TIER_AMBIENT)   total /= 4;
    else if (t->tier_best == TIER_NEAR) total = (total * 3) / 4;
    // CONTACT keeps full weight and already carries its own +25.

    if (total > 100) total = 100;
    if (total < 0)   total = 0;
    t->score = (uint8_t)total;
}

confidence_t classify_confidence(const track_t *t)
{
    if (!t) return CONF_LOW;
    if (t->score >= SCORE_HIGH)     return CONF_HIGH;
    if (t->score >= SCORE_LIKELY)   return CONF_LIKELY;
    if (t->score >= SCORE_POSSIBLE) return CONF_POSSIBLE;
    return CONF_LOW;
}

const char *confidence_name(confidence_t c)
{
    return c == CONF_HIGH ? "HIGH" : c == CONF_LIKELY ? "LIKELY"
         : c == CONF_POSSIBLE ? "POSSIBLE" : "low";
}

// Human-readable evidence for the detail screen. The operator must be able to
// see WHY, not just a score they have to take on faith.
uint8_t classify_explain(const track_t *t, char lines[][44], uint8_t maxl)
{
    uint8_t n = 0;
    if (!t) return 0;
    #define ADD(...) do { if (n < maxl) snprintf(lines[n++], 44, __VA_ARGS__); } while (0)

    // Lead with the triage verdict and the rule that produced it, so the
    // label can be judged rather than taken on faith.
    ADD("%s: %.34s", risk_name(t->risk), t->risk_why ? t->risk_why : "");
    if (t->dtype != DTYPE_UNKNOWN)
        ADD("type %s (%s)%s%.12s", dtype_name(t->dtype), dtier_name(t->dtype_tier),
            t->dtype_detail ? " " : "", t->dtype_detail ? t->dtype_detail : "");
    if (t->name[0])
        ADD("name %.36s", t->name);
    if (t->vendor[0])
        ADD("vendor %.20s%s", t->vendor, t->vendor_tier == DT_TIER_A ? " (WPS)" : "");
    if (t->model[0])
        ADD("model %.30s", t->model);
    if (t->dist_m >= 0.0f)
        ADD("%s ~%.2fm peak %ddBm", tier_name(t->tier), t->dist_m, (int)t->peak_session);
    else
        ADD("%s peak %ddBm (no TX ref)", tier_name(t->tier), (int)t->peak_session);

    if (t->evidence & E_WPS_DEVTYPE)    ADD("+ declared its type via WPS");
    if (t->evidence & E_BLE_APPEARANCE) ADD("+ declared BLE appearance 0x%04X", t->appearance);
    if (t->evidence & E_CONTACT_TIER)   ADD("+ hit CONTACT: not through a wall");
    if (t->evidence & E_ROOM_ONLY)      ADD("+ in-room only vs corridor");
    if (t->evidence & E_STREAM_PROFILE) ADD("+ streaming %lup/s %lukB up",
                                            (unsigned long)t->pkt_rate,
                                            (unsigned long)(t->bytes_up/1024));
    if (t->evidence & E_SOFTAP_CAM_SSID)ADD("+ camera SoftAP: %.16s", t->name);
    if (t->evidence & E_CAM_OUI_STRONG)
        ADD("+ camera vendor OUI: %.14s", t->vendor[0] ? t->vendor : "?");
    if (t->evidence & E_BLE_CAM_NAME)   ADD("+ BLE camera name: %.16s", t->name);
    if (t->evidence & E_BLE_MIC_NAME)   ADD("+ BLE mic name: %.18s", t->name);
    if (t->evidence & E_BLE_SVC_UUID)   ADD("+ BLE service UUID of interest");
    if (t->evidence & E_BROAD_PEAK)     ADD("+ broad peak (in-room shape)");
    if (t->evidence & E_SHARP_PEAK)     ADD("~ sharp peak: wall or fixture");
    if (t->evidence & E_CAM_OUI_WEAK)   ADD("~ weak OUI/keyword only");
    if (t->evidence & E_WILDCARD_PROBE) ADD("~ broadcast probe, empty SSID");
    if (t->evidence & E_HIDDEN_SSID)    ADD("~ beaconing, hidden SSID");
    if (n == 1) ADD("no identity evidence yet");
    #undef ADD
    return n;
}
