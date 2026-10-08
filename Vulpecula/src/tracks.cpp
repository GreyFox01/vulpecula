// tracks.cpp - PSRAM track table and the corridor/room baseline diff.
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
// TRACK TABLE
//
// Ambient-tier observations are deliberately KEPT rather than discarded: the
// corridor/room baseline diff is the real out-of-room filter and it only
// works if sub-threshold history survives.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

static track_t *g_tracks = NULL;
uint16_t g_used = 0;
phase_t  g_phase = PHASE_FREE;
// Concurrency: the pump task (priority 5) writes tracks while the UI task
// reads them. There is deliberately NO lock. Nothing here can crash on a race
// because the table is a fixed PSRAM array that is never freed, so a torn
// read of a counter or a float costs at most one garbled pixel row for 220 ms.
// Taking a spinlock in the capture hot path to buy that back would cost real
// frames, and dropped frames lose evidence. The one race that DID matter -
// the detail screen holding a pointer to a slot that got evicted and reused
// for a different device - is fixed by keying the selection on MAC and band
// and re-resolving it on every draw. See s_sel_mac below.

void classify_track(track_t *t);        // forward
// The imported-database lookups live further down, next to the import
// code that fills them, but the classifier is the only caller.
const oui_rec_t *oui_db_lookup(const uint8_t mac[6]);
bool namesig_match(const char *name, bool is_ble,
                          uint8_t *dtype, uint8_t *weight);
void triage_update(track_t *t, uint32_t now);   // forward

void tracks_reset()
{
    if (!g_tracks) return;
    memset(g_tracks, 0, (size_t)MAX_TRACKS * sizeof(track_t));
    g_used = 0;
    g_phase = PHASE_FREE;
}

bool tracks_init()
{
    if (g_tracks) return true;
    // PSRAM: 600 tracks is roughly 250 kB, uncomfortable in internal SRAM but
    // nothing against 8 MB. Requires PSRAM: Enabled in the Tools menu.
    g_tracks = (track_t *)heap_caps_calloc(MAX_TRACKS, sizeof(track_t),
                                           MALLOC_CAP_SPIRAM);
    if (!g_tracks) {
        Serial.println("[tracks] PSRAM alloc failed - is PSRAM enabled in Tools?");
        g_tracks = (track_t *)calloc(MAX_TRACKS, sizeof(track_t));
    }
    if (!g_tracks) { Serial.println("[tracks] FATAL: no memory"); return false; }
    tracks_reset();
    return true;
}

uint16_t mac_hash(const uint8_t mac[6], rband_t band)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= mac[i]; h *= 16777619u; }
    h ^= (uint32_t)band; h *= 16777619u;
    return (uint16_t)(h % MAX_TRACKS);
}

// A device heard on both bands gets separate tracks on purpose: the
// thresholds differ per band and mixing them would corrupt the gate.
track_t *track_get(const uint8_t mac[6], rband_t band, bool create)
{
    if (!g_tracks) return NULL;
    uint16_t idx = mac_hash(mac, band);
    int16_t  freeslot = -1;

    for (uint16_t p = 0; p < MAX_TRACKS; p++) {
        uint16_t i = (uint16_t)((idx + p) % MAX_TRACKS);
        track_t *t = &g_tracks[i];
        if (!t->used) { freeslot = (int16_t)i; break; }
        if (t->band == band && memcmp(t->mac, mac, 6) == 0) return t;
    }
    if (!create) return NULL;

    if (freeslot < 0) {
        // Table full: evict the stalest ambient track. Never evict anything
        // that has ever reached NEAR - that is the operator's evidence.
        uint32_t oldest = UINT32_MAX; int16_t victim = -1;
        for (uint16_t i = 0; i < MAX_TRACKS; i++) {
            if (g_tracks[i].tier_best >= TIER_NEAR) continue;
            if (g_tracks[i].last_ms < oldest) { oldest = g_tracks[i].last_ms; victim = (int16_t)i; }
        }
        if (victim < 0) return NULL;
        freeslot = victim;
        if (g_used) g_used--;
    }

    track_t *t = &g_tracks[freeslot];
    memset(t, 0, sizeof(track_t));
    t->used = true;
    memcpy(t->mac, mac, 6);
    t->band = band;
    t->peak_session = INT8_MIN;
    t->dist_m = -1.0f;
    t->ref.src = REF_NONE;
    ring_reset(&t->ring);
    for (int p = 0; p < PHASE_COUNT; p++) t->peak_phase[p] = INT8_MIN;
    g_used++;
    return t;
}

// RSSI ATTRIBUTION RULE: call this only for the actual transmitter. For WiFi
// that means addr2 and nothing else. Crediting RSSI to addr1 would let a
// distant camera inherit the signal strength of a nearby AP talking to it,
// which would silently destroy the gate.
void track_observe_rssi(track_t *t, int8_t rssi, uint32_t now)
{
    if (!t) return;
    if (t->first_ms == 0) t->first_ms = now;
    t->last_ms = now;
    t->pkts++;
    ring_push(&t->ring, rssi, now);
    if (rssi > t->peak_session) t->peak_session = rssi;

    if (g_phase != PHASE_FREE && rssi > t->peak_phase[g_phase]) {
        t->peak_phase[g_phase] = rssi;
        t->phase_mask |= (uint8_t)(1u << g_phase);
    }

    if (t->rate_win_start == 0 ||
        (uint32_t)(now - t->rate_win_start) >= STREAM_WINDOW_MS) {
        uint32_t el = now - t->rate_win_start;
        if (t->rate_win_start && el) t->pkt_rate = (t->rate_win_pkts * 1000u) / el;
        t->rate_win_start = now;
        t->rate_win_pkts = 0;
    }
    t->rate_win_pkts++;
}

void tracks_tick(uint32_t now)
{
    if (!g_tracks) return;
    for (uint16_t i = 0; i < MAX_TRACKS; i++) {
        track_t *t = &g_tracks[i];
        if (!t->used) continue;
        const ranging_ref_t *ref = (t->ref.src != REF_NONE) ? &t->ref : NULL;

        tier_t prev = t->tier;
        t->tier   = prox_resolve_tier(&t->ring, t->band, ref, prev, now);
        t->dist_m = prox_distance_m(&t->ring, ref, now);

        if (t->tier > t->tier_best) t->tier_best = t->tier;
        if (t->tier == TIER_CONTACT) t->evidence |= E_CONTACT_TIER;
        // Alert once per crossing up into NEAR, never repeatedly while a
        // device sits in range - repeated alerts train you to ignore the tool.
        if (t->tier >= TIER_NEAR && prev < TIER_NEAR) t->alerted = false;

        classify_track(t);
        triage_update(t, now);
    }
}

// Sweep peak shape. A device in the room shows a broad elevated region. One
// on the far side of a wall peaks only when you are against that wall. A
// device concealed in an in-room fixture also peaks sharply, but HIGHER -
// into the contact tier - which is how those two sharp cases are told apart.
void tracks_close_position(uint32_t now)
{
    if (!g_tracks) return;
    for (uint16_t i = 0; i < MAX_TRACKS; i++) {
        track_t *t = &g_tracks[i];
        if (!t->used) continue;

        t->profile[t->profile_head] = ring_peak(&t->ring, now, PEAK_WINDOW_MS * 4);
        t->profile_head = (uint8_t)((t->profile_head + 1) % SWEEP_PROFILE_SLOTS);
        if (t->profile_count < SWEEP_PROFILE_SLOTS) t->profile_count++;
        if (t->profile_count < 4) continue;

        int8_t best = INT8_MIN;
        for (uint8_t k = 0; k < t->profile_count; k++)
            if (t->profile[k] > best) best = t->profile[k];
        if (best == INT8_MIN) continue;

        uint8_t heard = 0, near_best = 0;
        for (uint8_t k = 0; k < t->profile_count; k++) {
            if (t->profile[k] == INT8_MIN) continue;
            heard++;
            if (t->profile[k] >= best - 8) near_best++;
        }
        if (!heard) continue;

        t->evidence &= ~(uint32_t)(E_SHARP_PEAK | E_BROAD_PEAK);
        float frac = (float)near_best / (float)heard;
        if (frac <= 0.34f)      t->evidence |= E_SHARP_PEAK;
        else if (frac >= 0.60f) t->evidence |= E_BROAD_PEAK;
    }
}

void baseline_apply_diff()
{
    if (!g_tracks) return;
    for (uint16_t i = 0; i < MAX_TRACKS; i++) {
        track_t *t = &g_tracks[i];
        if (!t->used) continue;
        bool in_room = (t->phase_mask & (1u << PHASE_ROOM)) != 0;
        bool in_cor  = (t->phase_mask & (1u << PHASE_CORRIDOR)) != 0;
        if (!in_room) continue;
        if (!in_cor) t->evidence |= E_ROOM_ONLY;      // strongest evidence here
        else if (t->peak_phase[PHASE_ROOM] - t->peak_phase[PHASE_CORRIDOR]
                 >= ROOM_ONLY_DELTA_DB) t->evidence |= E_ROOM_ONLY;
    }
}

// Baseline phase control. The corridor/room diff is the real out-of-room
// filter, and it needs a known dwell in each phase - hence the timestamp, so
// the sweep screen can show a countdown instead of leaving the operator
// guessing when 60 seconds are up.
uint32_t g_phase_start = 0;

phase_t baseline_phase(void) { return g_phase; }

void baseline_set_phase(phase_t p, uint32_t now)
{
    g_phase = p;
    g_phase_start = now;
}

uint32_t baseline_elapsed_ms(uint32_t now)
{
    return g_phase_start ? (now - g_phase_start) : 0;
}

// Highest risk first - that is the whole point of the list. Ties break on
// the gate tier, then the score, then raw signal, so ordering is fully
// determined and rows do not swap places on equal footing.
int cmp_tracks(const void *pa, const void *pb)
{
    const track_t *a = *(const track_t **)pa, *b = *(const track_t **)pb;
    if (a->risk != b->risk)           return (int)b->risk - (int)a->risk;
    if (a->tier_best != b->tier_best) return (int)b->tier_best - (int)a->tier_best;
    if (a->score != b->score)         return (int)b->score - (int)a->score;
    if (a->peak_session != b->peak_session)
                                      return (int)b->peak_session - (int)a->peak_session;
    return memcmp(a->mac, b->mac, 6);
}

// Show only devices whose signal is consistent with being in this room.
//
// This is the point of the whole instrument. A hotel puts 100-300 BLE devices
// and 50+ access points in the table; listing them all and merely SORTING the
// in-room ones to the top still produces a log file you have to read rather
// than an answer. Ambient tracks are still kept - the corridor/room baseline
// diff needs them - they are just not shown.
//
// Off by default so nothing is hidden silently, and the LIST header always
// says how many are filtered out.
bool s_show_all = false;

bool track_in_room(const track_t *t)
{
    if (t->tier_best >= TIER_NEAR) return true;
    // A device that was promoted by the baseline diff belongs in the room
    // even if it is momentarily quiet.
    if (t->evidence & E_ROOM_ONLY) return true;
    return false;
}

uint16_t tracks_sorted_filtered(track_t **out, uint16_t cap,
                                       bool all, uint16_t *hidden)
{
    if (!g_tracks) { if (hidden) *hidden = 0; return 0; }
    uint16_t n = 0, skipped = 0;
    for (uint16_t i = 0; i < MAX_TRACKS; i++) {
        if (!g_tracks[i].used) continue;
        if (!all && !track_in_room(&g_tracks[i])) { skipped++; continue; }
        if (n < cap) out[n++] = &g_tracks[i];
    }
    qsort(out, n, sizeof(track_t *), cmp_tracks);
    if (hidden) *hidden = skipped;
    return n;
}

uint16_t tracks_sorted(track_t **out, uint16_t cap)
{
    return tracks_sorted_filtered(out, cap, true, NULL);
}

// Indexed access for callers that need to walk the whole table - the alert
// scan in the pump task, mainly. Returns NULL for an unused slot, so the
// table's internals stay private to this module instead of g_tracks becoming
// a global everyone pokes at.
track_t *track_at(uint16_t idx)
{
    if (!g_tracks || idx >= MAX_TRACKS) return NULL;
    return g_tracks[idx].used ? &g_tracks[idx] : NULL;
}
