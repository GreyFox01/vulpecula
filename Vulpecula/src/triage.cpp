// triage.cpp - Triage rule table. PURE: test/triage_test.cpp includes it directly.
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
// TRIAGE
//
// Risk is two independent questions, and collapsing them into one number
// loses the distinction that matters:
//
//   1. how confident are we this is a recording device?   (dtype + tier)
//   2. how confident are we it is in THIS room?           (gate tier,
//                                                          baseline diff)
//
// The list has to be ordered, so the two are combined into a level - but the
// rule that fired is recorded and shown on the detail screen, so the label is
// auditable rather than an opaque score. A triage label is a claim; a
// CRITICAL on a soundbar costs the operator's trust permanently, and after
// that they ignore the tool.
//
// Policy: BALANCED. Strong type evidence at NEAR is enough for CRITICAL -
// contact range or a baseline diff is not required. The trade is accepted
// deliberately: in a hotel room a declared camera two metres away deserves
// the top of the list even if the operator never ran a corridor baseline.
// ===========================================================================

const char *risk_name(risk_t r)
{
    switch (r) {
        case RISK_CRITICAL: return "CRIT";
        case RISK_HIGH:     return "HIGH";
        case RISK_MEDIUM:   return "MED";
        default:            return "LOW";
    }
}

uint16_t risk_colour(risk_t r)
{
    switch (r) {
        case RISK_CRITICAL: return C_RED;
        case RISK_HIGH:     return C_AMBER;
        case RISK_MEDIUM:   return C_YELLOW;
        default:            return C_PANEL;
    }
}

// Evaluate the rule table. First match wins, and each rule carries the reason
// string that the detail screen shows.
risk_t triage_eval(const track_t *t, const char **why)
{
    bool recorder   = dtype_is_recorder(t->dtype);
    bool hard_type  = recorder && t->dtype_tier <= DT_TIER_B;  // declared or behaviour
    bool soft_type  = recorder && t->dtype_tier >  DT_TIER_B;  // name or vendor only
    bool contact    = (t->tier_best >= TIER_CONTACT);
    bool near       = (t->tier_best >= TIER_NEAR);
    bool room_only  = (t->evidence & E_ROOM_ONLY) != 0;
    bool streaming  = (t->evidence & E_STREAM_PROFILE) != 0;

    // ---- CRITICAL ----
    if (contact && recorder) {
        *why = "recorder at contact range";
        return RISK_CRITICAL;
    }
    if (near && hard_type) {
        // The balanced policy in one line.
        *why = (t->dtype_tier == DT_TIER_A) ? "declared recorder within gate"
                                            : "recording behaviour within gate";
        return RISK_CRITICAL;
    }
    if (near && room_only && recorder) {
        *why = "recorder, in-room only vs corridor";
        return RISK_CRITICAL;
    }

    // ---- HIGH ----
    if (near && soft_type) {
        *why = "possible recorder within gate";
        return RISK_HIGH;
    }
    if (contact && streaming) {
        *why = "streaming device at contact range";
        return RISK_HIGH;
    }
    if (near && room_only) {
        *why = "in-room only vs corridor baseline";
        return RISK_HIGH;
    }
    if (contact) {
        *why = "unidentified device at contact range";
        return RISK_HIGH;
    }

    // ---- MEDIUM ----
    if (near) {
        *why = t->evidence ? "within gate, some evidence" : "within gate";
        return RISK_MEDIUM;
    }
    if (hard_type) {
        // Out of the gate but the device declares itself a recorder. Capped
        // at MEDIUM on purpose: a perfect match in the next room is not this
        // operator's problem, and letting it climb would crowd out the thing
        // actually in the smoke detector.
        *why = "recorder, but outside the gate";
        return RISK_MEDIUM;
    }

    *why = "no proximity, no type evidence";
    return RISK_LOW;
}

// Level hysteresis. Without this the badge flips between levels on multipath
// exactly the way the RSSI tier used to, and a list whose labels churn is
// unreadable. A level must be sustained before it is shown, and DEMOTION is
// slower than promotion so a device does not quietly drop off the top of the
// list between glances.

void triage_update(track_t *t, uint32_t now)
{
    const char *why = NULL;
    risk_t raw = triage_eval(t, &why);
    t->risk_why = why;

    if (raw == t->risk) { t->risk_since = now; return; }

    if (t->risk_since == 0) { t->risk_since = now; t->risk_raw = raw; }
    if (raw != t->risk_raw) { t->risk_raw = raw; t->risk_since = now; return; }

    uint32_t need = (raw > t->risk) ? RISK_PROMOTE_MS : RISK_DEMOTE_MS;
    if ((uint32_t)(now - t->risk_since) >= need) {
        t->risk = raw;
        t->risk_since = now;
    }
}
