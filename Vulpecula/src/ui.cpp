// ui.cpp - Screens, touch routing, rendering.
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

// ---------------------------------------------------------------------------
// Screen data: touch targets and procedure-page text.
//
// Hoisted above the code deliberately. In the old single-file sketch this
// ordering had to fight the Arduino prototype injector; here it is ordinary
// declaration order, so the data simply comes first.
// ---------------------------------------------------------------------------
const rect_t BTN_ALL   = {  10,  52, 300, 46 };
const rect_t BTN_WIFI  = {  10, 104, 145, 46 };
const rect_t BTN_BLE   = { 165, 104, 145, 46 };
const rect_t BTN_WATCH = {  10, 156, 300, 34 };

const rect_t BTN_NEXTPASS = { 214, 166, 100, 22 };
// STOP on the sweep banner itself. This is the screen you are looking at
// while a scan runs, so it is where stopping has to be one tap away - it used
// to be reachable only from SETUP.
const rect_t BTN_SWSTOP   = { 236,   2,  78, 22 };

const rect_t BTN_LUP = { SCROLL_X, LIST_Y0,      SCROLL_W, 92 };
const rect_t BTN_LDN = { SCROLL_X, LIST_Y0 + 96,  SCROLL_W, 92 };

const rect_t BTN_INFO_BACK  = {   8, 200, 145, 32 };
const rect_t BTN_CALB24 = {   8, 172,  96, 22 };
const rect_t BTN_CALB5  = { 112, 172,  96, 22 };
const rect_t BTN_CALBLE = { 216, 172,  96, 22 };
const rect_t BTN_INFO_START = { 167, 200, 145, 32 };

const infopage_t INFO_BASELINE = {
    "BASELINE CAPTURE",
    {
        "Records what is audible in the corridor, then in",
        "the room, and flags devices heard ONLY inside.",
        "",
        "The 2m gate is the crude first pass: a neighbour's",
        "camera against the party wall can still cross it.",
        "Only the corridor-vs-room difference separates",
        "their hardware from yours.",
        "> 1. Stand OUTSIDE the door, then tap START.",
        "> 2. Wait 60s. Go inside, then tap ROOM >.",
        "> 3. Wait 60s, then tap APPLY.",
        "!Seen only inside = flagged R, and ranked up.",
        NULL
    }
};

const infopage_t INFO_WIPE = {
    "WIPE SWEEP LOGS",
    {
        "Deletes every vulp_NNN.csv sweep log and starts a",
        "fresh session with a NEW salt, so earlier readings",
        "cannot be correlated with later ones.",
        "",
        "MAC addresses are pseudonymised, but SSIDs, vendor",
        "and model strings are stored verbatim alongside",
        "timestamps. A lost or seized card is a record of",
        "where you swept and what was there.",
        "!Not a secure erase: this unlinks files and the",
        "!blocks remain until reused. Destroy the card if",
        "!flash imaging is in your threat model.",
        NULL
    }
};

const infopage_t INFO_RSSICAL = {
    "RSSI CALIBRATION",
    {
        "Measures what THIS board reports at 2.00m and at",
        "0.30m, so the gate is measured, not calculated.",
        "Shipped values are path-loss maths plus an assumed",
        "margin; antenna and enclosure move them several dB.",
        "",
        "Needs the reference beacon, a tape measure and a",
        "quiet room. Readings are corrected for the beacon's",
        "known transmit power.",
        "> 1. Board flat at chest height. Ref at 2.00m.",
        "> 2. Tap a BAND below - it locks the radio there.",
        "> 3. At 40+ samples move ref to 0.30m, tap 0.30M >.",
        NULL
    }
};

const rect_t BTN_PHASE = {   8,  24, 145, 28 };
const rect_t BTN_CAL   = { 167,  24, 145, 28 };
const rect_t BTN_TCAL2 = {   8, 134, 145, 26 };
const rect_t BTN_RESET = { 167, 134, 145, 26 };
const rect_t BTN_WIPE  = { 167, 162, 145, 26 };
const rect_t BTN_IMPORT= {   8, 162, 145, 26 };
const rect_t BTN_STOP  = {   8, 190, 304, 26 };


// ===========================================================================
// UI  -  five screens
//
//   WELCOME  mode selection. Static, painted once.
//   SWEEP    the physical hunt. Dirty-field readout, peak-hold bar, dwell.
//   LIST     triage, highest risk first, three lines per row.
//   DETAIL   the evidence and the triage rule that fired.
//   SETUP    baseline, calibration, diagnostics.
//
// Every screen paints its chrome once on entry and then updates only fields
// whose content changed. Nothing is cleared per frame; see FLICKER-FREE
// FIELDS above for why that was the whole problem.
// ===========================================================================

// Overlap one row when paging so the operator keeps their place in the list.
#define SCROLL_STEP (LIST_ROWS - 1)

screen_t s_screen = SCR_WELCOME;
uint8_t  s_screen_drawn = 0xFF;   // which screen's chrome is on glass
uint8_t  s_sel_mac[6] = {0};
rband_t  s_sel_band = BAND_24;
bool     s_sel_valid = false;
uint32_t s_last_draw = 0, s_last_touch = 0;
uint16_t s_list_top = 0;
// Screen-off used to have its own SETUP button. The import control took that
// slot, and the backlight is better handled by the SWEEP screen's own idle
// than by a toggle the operator has to remember to undo.
bool     s_quiet = false;

track_t *sel_track()
{
    if (!s_sel_valid) return NULL;
    return track_get(s_sel_mac, s_sel_band, false);
}

void ui_set_screen(screen_t s)
{
    s_screen = s;
    s_screen_drawn = 0xFF;      // forces a chrome repaint on the next tick
    s_last_draw = 0;
}

bool hit(const rect_t *r, uint16_t x, uint16_t y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

void draw_button(const rect_t *r, const char *label, const char *sub,
                        uint16_t frame, uint16_t label_col)
{
    draw_rect(r->x, r->y, r->w, r->h, frame);
    int lw = (int)strlen(label) * 12;
    draw_text_12((int16_t)(r->x + (r->w - lw) / 2),
                 (int16_t)(r->y + (sub ? 6 : (r->h - 16) / 2)), label,
                 label_col, C_BLACK);
    if (sub) {
        int sw = (int)strlen(sub) * 6;
        draw_text((int16_t)(r->x + (r->w - sw) / 2), (int16_t)(r->y + 26), sub,
                  C_DIM, C_BLACK);
    }
}

// ===========================================================================
// WELCOME  (static: painted once, never refreshed)
// ===========================================================================



// TOUCH CAL used to sit beside WATCH here. It moved to SETUP alongside the
// other calibration, so WATCH takes the full width.

// Menu items are filled blocks with the label inside, rather than outlines
// with text floating in them: at arm's length a filled rectangle reads as a
// button and an outline reads as a box around some words.
void draw_menu_item(const rect_t *r, const char *label, const char *sub,
                           uint16_t fill)
{
    fill_rect(r->x, r->y, r->w, r->h, fill);
    int lw = (int)strlen(label) * 12;
    draw_text_12((int16_t)(r->x + (r->w - lw) / 2),
                 (int16_t)(r->y + (sub ? 8 : (r->h - 16) / 2)), label,
                 C_BLACK, fill);
    if (sub) {
        int sw = (int)strlen(sub) * 6;
        draw_text((int16_t)(r->x + (r->w - sw) / 2), (int16_t)(r->y + 29), sub,
                  C_BLACK, fill);
    }
}

void draw_welcome_chrome()
{
    fill_screen(C_BLACK);
    draw_text_12(8, 6, "VULPECULA", C_CYAN, C_BLACK);
    draw_text(128, 11, "BUG SWEEPER", C_DIM, C_BLACK);
    draw_hline(0, 28, SCR_W, C_PANEL);

    draw_menu_item(&BTN_ALL,   "SWEEP ALL", "WI-FI PASS, THEN BLE PASS", C_CYAN);
    draw_menu_item(&BTN_WIFI,  "WI-FI",     "2.4 + 5 GHZ",               C_AMBER);
    draw_menu_item(&BTN_BLE,   "BLE",       "PASSIVE",                   C_AMBER);
    draw_menu_item(&BTN_WATCH, "WATCH",     NULL,                        C_PANEL);

    // One status line, not four. Everything else lives on SETUP, where it can
    // be read without competing with the menu.
    char b[56];
    draw_text(8, 210, s_mode == MODE_IDLE ? "RADIOS OFF - PICK A SWEEP"
                                          : "RADIOS RUNNING",
              s_mode == MODE_IDLE ? C_DIM : C_AMBER, C_BLACK);

    snprintf(b, sizeof(b), "%u x 5GHZ%s   RSSI %s   OUI %lu",
             (unsigned)(s_probed_5g ? s_n_ch5 : 0),
             s_probed_5g ? (s_dfs_ok ? " DFS" : "") : " (ON START)",
             g_cal.calibrated[BAND_24] ? "CAL" : "DEFAULT",
             (unsigned long)g_oui_n);
    draw_text(8, 222, b,
              g_cal.calibrated[BAND_24] ? C_GREEN : C_AMBER, C_BLACK);
}

// ===========================================================================
// tab bar
// ===========================================================================
const char *TAB_NAMES[SCR_TAB_COUNT] = { "HOME", "SWEEP", "LIST", "INFO", "SETUP" };

void draw_tabbar()
{
    const int tw = SCR_W / SCR_TAB_COUNT;
    for (int i = 0; i < SCR_TAB_COUNT; i++) {
        int x = i * tw;
        bool act = (i == (int)s_screen);
        uint16_t bg = act ? C_CYAN : C_PANEL, fg = act ? C_BLACK : C_DIM;
        fill_rect(x, TABBAR_Y, tw - 1, TAB_H, bg);
        int lw = (int)strlen(TAB_NAMES[i]) * 6;
        draw_text(x + (tw - 1 - lw) / 2, TABBAR_Y + 7, TAB_NAMES[i], fg, bg);
    }
}

// ===========================================================================
// SWEEP
// ===========================================================================

static field_t F_TIER, F_STATUS, F_PEAK, F_DIST, F_IDENT, F_RISK;
static field_t F_HELD, F_COVER, F_COUNTS, F_PROMPT, F_BASE;
bar_t   B_PEAK;
uint16_t s_tier_bg = 0xFFFF;


void draw_sweep_chrome()
{
    fill_screen(C_BLACK);
    draw_rect(BAR_X - 1, BAR_Y - 1, BAR_W + 2, BAR_H + 2, C_PANEL);
    draw_text(6,   BAR_Y + BAR_H + 4, "-90", C_DIM, C_BLACK);
    draw_text(296, BAR_Y + BAR_H + 4, "-10", C_DIM, C_BLACK);

    // Gate ticks are chrome: they only move when calibration changes.
    rband_t ab = (sched_active_proto() == BAND_BLE) ? BAND_BLE : BAND_24;
    draw_vline(BAR_X + ((g_cal.near_thresh[ab]    + 90) * BAR_W) / 80,
               BAR_Y - 4, BAR_H + 8, C_AMBER);
    draw_vline(BAR_X + ((g_cal.contact_thresh[ab] + 90) * BAR_W) / 80,
               BAR_Y - 4, BAR_H + 8, C_RED);

    field_init(&F_TIER,   6,   5, 8, 1, C_BLACK, C_GREEN);
    field_init(&F_STATUS, 108, 9, 20, 0, C_BLACK, C_GREEN);
    field_init(&F_PEAK,   6,  32, 9, 1, C_WHITE, C_BLACK);
    field_init(&F_DIST,   194, 32, 10, 1, C_DIM,  C_BLACK);
    field_init(&F_RISK,   6,  54, 52, 0, C_DIM,  C_BLACK);
    field_init(&F_IDENT,  6, 106, 52, 0, C_WHITE, C_BLACK);
    field_init(&F_HELD,   6, 120, 52, 0, C_DIM,  C_BLACK);
    field_init(&F_COVER,  6, 134, 52, 0, C_DIM,  C_BLACK);
    field_init(&F_COUNTS, 6, 148, 52, 0, C_DIM,  C_BLACK);
    field_init(&F_PROMPT, 6, 168, 33, 0, C_DIM,  C_BLACK);
    field_init(&F_BASE,   6, 196, 52, 0, C_CYAN, C_BLACK);
    bar_init(&B_PEAK, BAR_X, BAR_Y, BAR_W, BAR_H);
    s_tier_bg = 0xFFFF;
    draw_tabbar();
}

void draw_sweep(uint32_t now)
{
    static track_t *list[64];
    uint16_t n = tracks_sorted(list, 64);
    track_t *top = n ? list[0] : NULL;

    int8_t peak = INT8_MIN;
    tier_t tier = TIER_AMBIENT;
    if (top) { peak = ring_peak(&top->ring, now, PEAK_WINDOW_MS); tier = top->tier; }
    sched_note_strongest(peak, now);

    if (tier == TIER_CONTACT)   led_rgb(50, 0, 0);
    else if (tier == TIER_NEAR) led_rgb(45, 22, 0);
    else                        led_rgb(0, 8, 0);

    uint16_t col = tier_colour(tier);
    char b[56];

    // Banner background is only repainted when the tier actually changes.
    if (col != s_tier_bg) {
        s_tier_bg = col;
        fill_rect(0, 0, SCR_W, 26, col);
        F_TIER.bg = col;   F_TIER.fg = C_BLACK;   field_invalidate(&F_TIER);
        F_STATUS.bg = col; F_STATUS.fg = C_BLACK; field_invalidate(&F_STATUS);
    }
    field_set(&F_TIER, tier_name(tier));
    field_set(&F_STATUS, sched_status());

    // Redrawn whenever the banner is refilled, since the fill wipes it.
    draw_rect(BTN_SWSTOP.x, BTN_SWSTOP.y, BTN_SWSTOP.w, BTN_SWSTOP.h, C_BLACK);
    draw_text(BTN_SWSTOP.x + 21, BTN_SWSTOP.y + 7, "STOP", C_BLACK, col);

    if (peak == INT8_MIN) snprintf(b, sizeof(b), "  -- dBm");
    else                  snprintf(b, sizeof(b), "%4d dBm", (int)peak);
    field_set(&F_PEAK, b);

    if (top && top->dist_m >= 0.0f) snprintf(b, sizeof(b), "~%.2f m", top->dist_m);
    else                            snprintf(b, sizeof(b), "no TX ref");
    field_set(&F_DIST, b);

    int fill = (peak == INT8_MIN) ? 0
             : ((((peak < -90 ? -90 : (peak > -10 ? -10 : peak)) + 90) * BAR_W) / 80);
    bar_set(&B_PEAK, (int16_t)fill, col);

    // Triage verdict for the strongest track, with the rule that produced it.
    if (top) {
        snprintf(b, sizeof(b), "%-4s %-8s %.34s", risk_name(top->risk),
                 dtype_name(top->dtype), top->risk_why ? top->risk_why : "");
        field_set_col(&F_RISK, b, risk_colour(top->risk) == C_PANEL
                                  ? C_DIM : risk_colour(top->risk));
    } else {
        field_set_col(&F_RISK, "", C_DIM);
    }

    if (top) {
        const char *who = top->vendor[0] ? top->vendor
                        : (top->name[0] ? top->name : "(no name)");
        snprintf(b, sizeof(b), "%-4s ch%-3u %.40s", band_name(top->band),
                 (unsigned)top->channel, who);
        field_set_col(&F_IDENT, b, C_WHITE);
    } else {
        field_set_col(&F_IDENT, "nothing above ambient", C_DIM);
    }

    uint32_t rms = sched_position_radio_ms(), cov = sched_covered_interval_ms();
    snprintf(b, sizeof(b), "held %.1fs radio at this position", rms / 1000.0f);
    field_set(&F_HELD, b);
    if (!cov) snprintf(b, sizeof(b), "covers: nothing yet");
    else      snprintf(b, sizeof(b), "covers transmit intervals <= %.2fs", cov / 1000.0f);
    field_set_col(&F_COVER, b, cov >= 1000 ? C_GREEN : C_AMBER);
    snprintf(b, sizeof(b), "tracks %u   positions %u   ident %lu",
             (unsigned)g_used, (unsigned)s_positions, (unsigned long)s_ie_seen);
    field_set(&F_COUNTS, b);

    if (s_pass_prompt) {
        field_set_col(&F_PROMPT, "PASS 2/2 - WALK THE ROOM AGAIN", C_CYAN);
    } else if (sched_sweep_too_fast()) {
        field_set_col(&F_PROMPT, "SWEEPING TOO FAST - hold still", C_YELLOW);
    } else {
        field_set_col(&F_PROMPT, "tap body to mark a position", C_DIM);
        if (s_mode == MODE_SWEEP_ALL && s_all_phase == 0) {
            draw_rect(BTN_NEXTPASS.x, BTN_NEXTPASS.y, BTN_NEXTPASS.w,
                      BTN_NEXTPASS.h, C_CYAN);
            draw_text(BTN_NEXTPASS.x + 14, BTN_NEXTPASS.y + 7, "BLE PASS >",
                      C_CYAN, C_BLACK);
        }
    }

    if (baseline_phase() != PHASE_FREE) {
        uint32_t el = baseline_elapsed_ms(now);
        uint32_t left = el >= BASELINE_CAPTURE_MS ? 0 : (BASELINE_CAPTURE_MS - el) / 1000;
        snprintf(b, sizeof(b), "BASELINE %s  %lus remaining",
                 baseline_phase() == PHASE_CORRIDOR ? "CORRIDOR" : "ROOM",
                 (unsigned long)left);
        field_set_col(&F_BASE, b, left ? C_CYAN : C_GREEN);
    } else {
        field_set(&F_BASE, "");
    }
}

// ===========================================================================
// LIST  -  six rows, three lines each, highest risk first
//
//   RISK  TYPE      name or SSID
//         vendor / model
//         band ch  peak  dist  evidence letters
// ===========================================================================


field_t F_LHDR;
field_t F_ROW[LIST_ROWS][3];
uint16_t s_row_stripe[LIST_ROWS];
// Re-sorting at the redraw rate makes rows churn even with zero flicker, so
// the order is refreshed on its own slower cadence.
uint32_t s_last_sort = 0;
static track_t *s_lview[MAX_TRACKS];
uint16_t s_lview_n = 0;
uint16_t s_lview_hidden = 0;

// Solid triangles read better than a caret glyph at this size.
void draw_tri(int16_t cx, int16_t cy, int16_t half, bool up, uint16_t c)
{
    for (int16_t i = 0; i < half; i++) {
        int16_t w = (int16_t)(2 * (half - i) - 1);
        int16_t y = up ? (int16_t)(cy + i) : (int16_t)(cy - i);
        fill_rect((int16_t)(cx - (half - i) + 1), y, w, 1, c);
    }
}

void draw_scroll_col(uint16_t n)
{
    bool can_up = (s_list_top > 0);
    bool can_dn = (n > LIST_ROWS) && (s_list_top + LIST_ROWS < n);

    fill_rect(BTN_LUP.x, BTN_LUP.y, BTN_LUP.w, BTN_LUP.h, C_BLACK);
    fill_rect(BTN_LDN.x, BTN_LDN.y, BTN_LDN.w, BTN_LDN.h, C_BLACK);
    draw_rect(BTN_LUP.x, BTN_LUP.y, BTN_LUP.w, BTN_LUP.h,
              can_up ? C_CYAN : C_PANEL);
    draw_rect(BTN_LDN.x, BTN_LDN.y, BTN_LDN.w, BTN_LDN.h,
              can_dn ? C_CYAN : C_PANEL);
    draw_tri((int16_t)(BTN_LUP.x + BTN_LUP.w / 2),
             (int16_t)(BTN_LUP.y + 36), 8, true,  can_up ? C_CYAN : C_PANEL);
    draw_tri((int16_t)(BTN_LDN.x + BTN_LDN.w / 2),
             (int16_t)(BTN_LDN.y + 56), 8, false, can_dn ? C_CYAN : C_PANEL);

    // Position thumb: shows how far down a long list you are, which the
    // "7-12 of 34" header alone does not convey at a glance.
    int16_t tx = (int16_t)(BTN_LUP.x + 2), tw = (int16_t)(SCROLL_W - 4);
    int16_t ty = (int16_t)(BTN_LUP.y + 76), th = 16;
    fill_rect(tx, ty, tw, th, C_BLACK);
    if (n > LIST_ROWS) {
        int16_t pos = (int16_t)((s_list_top * (tw - 4)) / (n - LIST_ROWS));
        fill_rect(tx, (int16_t)(ty + 6), tw, 2, C_PANEL);
        fill_rect((int16_t)(tx + pos), (int16_t)(ty + 4), 4, 6, C_CYAN);
    }
}

void draw_list_chrome()
{
    fill_screen(C_BLACK);
    fill_rect(0, 0, SCR_W, 20, C_PANEL);
    field_init(&F_LHDR, 4, 3, 52, 0, C_CYAN, C_PANEL);
    field_invalidate(&F_LHDR);
    draw_text(4, 12, "S STREAM R ROOM-ONLY C CONTACT A CAM-AP N NAME",
              C_DIM, C_PANEL);
    for (int r = 0; r < LIST_ROWS; r++) {
        int y = LIST_Y0 + r * ROW_H;
        field_init(&F_ROW[r][0], 8,  y,      ROW_CHARS, 0, C_WHITE, C_BLACK);
        field_init(&F_ROW[r][1], 8,  y + 10, ROW_CHARS, 0, C_DIM,   C_BLACK);
        field_init(&F_ROW[r][2], 8,  y + 20, ROW_CHARS, 0, C_DIM,   C_BLACK);
        s_row_stripe[r] = 0xFFFF;
    }
    draw_tabbar();
}

void draw_list(uint32_t now)
{
    if (s_last_sort == 0 || (uint32_t)(now - s_last_sort) >= LIST_SORT_MS) {
        s_last_sort = now;
        s_lview_n = tracks_sorted_filtered(s_lview, MAX_TRACKS,
                                          s_show_all, &s_lview_hidden);
    }
    uint16_t n = s_lview_n;
    char b[60];

    if (s_show_all)
        snprintf(b, sizeof(b), "ALL %u   %u-%u   tap here: IN-ROOM ONLY",
                 (unsigned)n, (unsigned)(n ? s_list_top + 1 : 0),
                 (unsigned)(s_list_top + LIST_ROWS < n ? s_list_top + LIST_ROWS : n));
    else
        snprintf(b, sizeof(b), "IN ROOM %u   %u hidden   tap here: SHOW ALL",
                 (unsigned)n, (unsigned)s_lview_hidden);
    field_set_col(&F_LHDR, b, s_show_all ? C_AMBER : C_CYAN);

    // Clamp so the last page is full rather than leaving blank rows below a
    // partially scrolled list.
    uint16_t max_top = (n > LIST_ROWS) ? (uint16_t)(n - LIST_ROWS) : 0;
    if (s_list_top > max_top) s_list_top = max_top;

    static uint16_t drawn_top = 0xFFFF, drawn_n = 0xFFFF;
    if (s_list_top != drawn_top || n != drawn_n) {
        drawn_top = s_list_top; drawn_n = n;
        draw_scroll_col(n);
    }

    for (int r = 0; r < LIST_ROWS; r++) {
        uint16_t idx = (uint16_t)(s_list_top + r);
        int y = LIST_Y0 + r * ROW_H;

        if (idx >= n) {
            if (s_row_stripe[r] != C_BLACK) {
                fill_rect(0, y, 4, ROW_H - 4, C_BLACK);
                s_row_stripe[r] = C_BLACK;
            }
            field_set(&F_ROW[r][0], "");
            field_set(&F_ROW[r][1], "");
            field_set(&F_ROW[r][2], "");
            continue;
        }

        track_t *t = s_lview[idx];
        uint16_t rc = risk_colour(t->risk);
        if (rc != s_row_stripe[r]) {
            fill_rect(0, y, 4, ROW_H - 4, rc);
            s_row_stripe[r] = rc;
        }

        // line 1: risk + device type + name
        snprintf(b, sizeof(b), "%-4s %-8s %.36s", risk_name(t->risk),
                 dtype_name(t->dtype),
                 t->name[0] ? t->name : "(unnamed)");
        field_set_col(&F_ROW[r][0], b, rc == C_PANEL ? C_DIM : rc);

        // line 2: vendor and model, with where the vendor came from
        if (t->vendor[0] || t->model[0])
            snprintf(b, sizeof(b), "  %.20s %.20s%s", t->vendor,
                     t->model, t->vendor_tier == DT_TIER_A ? " *" : "");
        else
            snprintf(b, sizeof(b), "  vendor unknown");
        field_set_col(&F_ROW[r][1], b, C_WHITE);

        // line 3: signal, distance and evidence letters
        char ev[8]; uint8_t k = 0;
        if (t->evidence & E_STREAM_PROFILE)  ev[k++] = 'S';
        if (t->evidence & E_ROOM_ONLY)       ev[k++] = 'R';
        if (t->evidence & E_CONTACT_TIER)    ev[k++] = 'C';
        if (t->evidence & E_SOFTAP_CAM_SSID) ev[k++] = 'A';
        if (t->evidence & (E_BLE_CAM_NAME | E_BLE_MIC_NAME)) ev[k++] = 'N';
        ev[k] = 0;
        if (t->dist_m >= 0.0f)
            snprintf(b, sizeof(b), "  %-4s ch%-3u %4ddBm ~%.2fm  %s",
                     band_name(t->band), (unsigned)t->channel,
                     (int)t->peak_session, t->dist_m, ev);
        else
            snprintf(b, sizeof(b), "  %-4s ch%-3u %4ddBm  %-8s %s",
                     band_name(t->band), (unsigned)t->channel,
                     (int)t->peak_session, dtier_name(t->dtype_tier), ev);
        field_set_col(&F_ROW[r][2], b, C_DIM);
    }
}

// ===========================================================================
// DETAIL
// ===========================================================================
void draw_detail_chrome()
{
    fill_screen(C_BLACK);
    draw_tabbar();
}

void draw_detail(uint32_t now)
{
    (void)now;
    static char shown[12][44];
    static uint8_t shown_n = 0;
    // Keyed on the FULL identity. The previous version compared only
    // mac[5], so two tracks sharing a last byte would show each other's
    // header, name included.
    static uint8_t  last_mac[6] = {0};
    static rband_t  last_band = BAND_COUNT;
    static bool     last_valid = false;

    track_t *t = sel_track();
    if (!t || !t->used) {
        if (shown_n != 0xFE) {
            fill_rect(0, 0, SCR_W, TABBAR_Y, C_BLACK);
            draw_text_12(8, 14, s_sel_valid ? "TRACK LOST" : "NO SELECTION",
                         C_DIM, C_BLACK);
            draw_text(8, 40, s_sel_valid
                      ? "That device is no longer being tracked."
                      : "Tap a row on the LIST screen.", C_DIM, C_BLACK);
            shown_n = 0xFE;
        }
        return;
    }

    bool changed = !last_valid || last_band != t->band ||
                   memcmp(last_mac, t->mac, 6) != 0 || shown_n == 0xFE;
    if (changed) {
        memcpy(last_mac, t->mac, 6);
        last_band  = t->band;
        last_valid = true;
        shown_n = 0;
        memset(shown, 0, sizeof(shown));
        fill_rect(0, 0, SCR_W, TABBAR_Y, C_BLACK);

        uint16_t rc = risk_colour(t->risk);
        uint16_t hb = (rc == C_PANEL) ? C_PANEL : rc;
        fill_rect(0, 0, SCR_W, 18, hb);
        char h[56];
        snprintf(h, sizeof(h), "%-4s  %s", risk_name(t->risk), dtype_name(t->dtype));
        draw_text(4, 5, h, C_BLACK, hb);

        // The device name gets the full width of its own line. Sharing a line
        // with the risk badge and the type meant a long SSID was the thing
        // that got truncated, which is backwards: the name is what the
        // operator reads out, writes down and searches for.
        char nm[56];
        snprintf(nm, sizeof(nm), "NAME %s",
                 t->name[0] ? t->name : "(none advertised)");
        draw_text(4, 22, nm, t->name[0] ? C_WHITE : C_DIM, C_BLACK);

        char pseudo[20], sub[60];
        log_pseudonym(t->mac, pseudo, sizeof(pseudo));
        snprintf(sub, sizeof(sub), "%s CH%u  ID %s  %lu PKTS", band_name(t->band),
                 (unsigned)t->channel, pseudo, (unsigned long)t->pkts);
        draw_text(4, 34, sub, C_DIM, C_BLACK);
    }

    // Only rewrite evidence lines whose text changed.
    // 12 lines at 14px from y=48 ends at 216, one pixel clear of the tab bar.
    char lines[12][44];
    uint8_t nl = classify_explain(t, lines, 12);
    for (uint8_t i = 0; i < 12; i++) {
        const char *want = (i < nl) ? lines[i] : "";
        if (strncmp(shown[i], want, 44) == 0) continue;
        char pad[48];
        snprintf(pad, sizeof(pad), "%-44s", want);
        uint16_t c = want[0] == '+' ? C_WHITE
                   : want[0] == '~' ? C_YELLOW : C_CYAN;
        draw_text(6, 48 + i * 14, pad, c, C_BLACK);
        strncpy(shown[i], want, 43); shown[i][43] = 0;
    }
    shown_n = nl;
}

// ===========================================================================
// PROCEDURE PAGES
//
// Baseline capture and RSSI calibration are both multi-step procedures with a
// right and a wrong way to run them, and neither is inferable from a button
// label. Run either one wrong and the result is worse than not running it:
// a baseline taken without leaving the room flags nothing, and a calibration
// taken with the reference in the wrong place moves the gate thresholds to
// values the operator then trusts.
//
// So each is fronted by a page that says what it does, what it needs, and how
// to run it, with START and BACK at the bottom. Nothing begins until START.
// ===========================================================================



static field_t F_INFO_STAT, F_INFO_BTN;

void draw_info_page(const infopage_t *p)
{
    fill_screen(C_BLACK);
    draw_text_12(6, 4, p->title, C_CYAN, C_BLACK);
    draw_hline(0, 23, SCR_W, C_PANEL);

    for (int i = 0; i < INFO_MAX_LINES && p->lines[i]; i++) {
        const char *l = p->lines[i];
        // A leading '!' marks a warning line, '>' a numbered step.
        uint16_t c = (l[0] == '!') ? C_AMBER : (l[0] == '>') ? C_WHITE : C_DIM;
        draw_text(6, 28 + i * 12, (l[0] == '!' || l[0] == '>') ? l + 1 : l,
                  c, C_BLACK);
    }

    draw_rect(BTN_INFO_BACK.x, BTN_INFO_BACK.y, BTN_INFO_BACK.w,
              BTN_INFO_BACK.h, C_PANEL);
    draw_text_12(BTN_INFO_BACK.x + 43, BTN_INFO_BACK.y + 8, "BACK",
                 C_DIM, C_BLACK);

    draw_rect(BTN_INFO_START.x, BTN_INFO_START.y, BTN_INFO_START.w,
              BTN_INFO_START.h, C_CYAN);

    // Band selection, RSSI CAL only. Explicit, because inheriting it from the
    // active sweep was a coin flip on a dual-band pass.
    if (p == &INFO_RSSICAL) {
        draw_rect(BTN_CALB24.x, BTN_CALB24.y, BTN_CALB24.w, BTN_CALB24.h, C_AMBER);
        draw_rect(BTN_CALB5.x,  BTN_CALB5.y,  BTN_CALB5.w,  BTN_CALB5.h,  C_AMBER);
        draw_rect(BTN_CALBLE.x, BTN_CALBLE.y, BTN_CALBLE.w, BTN_CALBLE.h, C_AMBER);
        draw_text(BTN_CALB24.x + 30, BTN_CALB24.y + 7, "2.4G", C_AMBER, C_BLACK);
        draw_text(BTN_CALB5.x  + 36, BTN_CALB5.y  + 7, "5G",   C_AMBER, C_BLACK);
        draw_text(BTN_CALBLE.x + 33, BTN_CALBLE.y + 7, "BLE",  C_AMBER, C_BLACK);
    }

    // Live status sits just above the buttons: sample count while
    // calibrating, phase countdown while baselining.
    field_init(&F_INFO_STAT, 6, 160, 51, 0, C_AMBER, C_BLACK);
    field_init(&F_INFO_BTN, BTN_INFO_START.x + 8, BTN_INFO_START.y + 8,
               10, 1, C_CYAN, C_BLACK);
}

// ---------------------------------------------------------------------------
// Baseline capture
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RSSI calibration
// ---------------------------------------------------------------------------

// The action button names the next step, so a tap is never a guess.
const char *info_action_label(const infopage_t *p)
{
    if (p == &INFO_WIPE) return "WIPE NOW";
    if (p == &INFO_BASELINE) {
        switch (baseline_phase()) {
            case PHASE_CORRIDOR: return "ROOM >";
            case PHASE_ROOM:     return "APPLY";
            default:             return "START";
        }
    }
    switch (s_cal) {
        case CAL_NEAR:    return "0.30M >";
        case CAL_CONTACT: return "FINISH";
        case CAL_DONE:    return "COMMIT";
        case CAL_FAILED:  return "RETRY";
        default:          return "START";
    }
}

// Refreshed every tick while a procedure page is open.
void draw_info_live(const infopage_t *p, uint32_t now)
{
    char b[56];

    const char *lab = info_action_label(p);
    int pad = (10 - (int)strlen(lab)) / 2;
    snprintf(b, sizeof(b), "%*s%s", pad > 0 ? pad : 0, "", lab);
    field_set(&F_INFO_BTN, b);

    if (p == &INFO_WIPE) {
        snprintf(b, sizeof(b), "current log: %s", s_sd_ok ? s_fname : "none");
        field_set_col(&F_INFO_STAT, b, s_sd_ok ? C_AMBER : C_DIM);
        return;
    }
    if (p == &INFO_BASELINE) {
        if (baseline_phase() == PHASE_FREE) {
            field_set_col(&F_INFO_STAT, "Not started. Step out of the room first.",
                          C_DIM);
        } else {
            uint32_t el = baseline_elapsed_ms(now);
            uint32_t left = el >= BASELINE_CAPTURE_MS ? 0
                          : (BASELINE_CAPTURE_MS - el) / 1000;
            snprintf(b, sizeof(b), "%s CAPTURE - %lus left - %u tracks",
                     baseline_phase() == PHASE_CORRIDOR ? "CORRIDOR" : "ROOM",
                     (unsigned long)left, (unsigned)g_used);
            field_set_col(&F_INFO_STAT, b, left ? C_CYAN : C_GREEN);
        }
        return;
    }

    // "no samples" has several very different causes, so name the one that
    // applies instead of leaving a zero on screen.
    if (s_cal == CAL_NEAR || s_cal == CAL_CONTACT) {
        if (!s_cal_ref_seen) {
            snprintf(b, sizeof(b), "REFERENCE NOT SEEN on %s - %u samples",
                     band_name(s_cal_band), (unsigned)s_cal_n);
            field_set_col(&F_INFO_STAT, b, C_RED);
            return;
        }
        snprintf(b, sizeof(b), "%s  %u samples  last %d dBm", cal_prompt(),
                 (unsigned)s_cal_n, (int)s_cal_last_rssi);
        field_set_col(&F_INFO_STAT, b, s_cal_n >= 40 ? C_GREEN : C_AMBER);
        return;
    }

    switch (s_cal) {
        case CAL_IDLE:
            {
            rband_t cb = sched_active_proto();
            int corr = cal_ref_correction(cb);
            snprintf(b, sizeof(b), "Ready: %s band, ref correction %+d dB",
                     band_name(cb), corr);
        }
            field_set_col(&F_INFO_STAT, b, C_AMBER);
            break;
        case CAL_DONE:
            snprintf(b, sizeof(b), "MEASURED near %d  contact %d dBm",
                     (int)s_cal_near, (int)s_cal_contact);
            field_set_col(&F_INFO_STAT, b, C_GREEN);
            break;
        case CAL_FAILED:
            field_set_col(&F_INFO_STAT,
                          "FAILED - ref never moved, or no antenna fitted",
                          C_RED);
            break;
        default:
            snprintf(b, sizeof(b), "%s  %u samples", cal_prompt(),
                     (unsigned)s_cal_n);
            field_set_col(&F_INFO_STAT, b, s_cal_n >= 40 ? C_GREEN : C_AMBER);
            break;
    }
}

// ===========================================================================
// SETUP
// ===========================================================================






static field_t F_PHASEB, F_CALB, F_S5, F_SWIFI, F_SBLE, F_SSD, F_SGATE,
               F_SCAL, F_STOUCH, F_SQUIET, F_SIMP;

void draw_setup_chrome()
{
    fill_screen(C_BLACK);
    draw_text_12(6, 2, "SETUP", C_CYAN, C_BLACK);
    draw_rect(BTN_PHASE.x, BTN_PHASE.y, BTN_PHASE.w, BTN_PHASE.h, C_CYAN);
    draw_rect(BTN_CAL.x,   BTN_CAL.y,   BTN_CAL.w,   BTN_CAL.h,   C_AMBER);
    draw_rect(BTN_TCAL2.x, BTN_TCAL2.y, BTN_TCAL2.w, BTN_TCAL2.h, C_PANEL);
    draw_rect(BTN_RESET.x, BTN_RESET.y, BTN_RESET.w, BTN_RESET.h, C_PANEL);
    draw_rect(BTN_IMPORT.x, BTN_IMPORT.y, BTN_IMPORT.w, BTN_IMPORT.h, C_PANEL);
    draw_rect(BTN_WIPE.x,  BTN_WIPE.y,  BTN_WIPE.w,  BTN_WIPE.h,  C_PANEL);
    draw_rect(BTN_STOP.x,  BTN_STOP.y,  BTN_STOP.w,  BTN_STOP.h,  C_PANEL);
    draw_text(BTN_TCAL2.x + 38, BTN_TCAL2.y + 9, "TOUCH CAL",   C_DIM, C_BLACK);
    draw_text(BTN_RESET.x + 30, BTN_RESET.y + 9, "NEW SESSION", C_DIM, C_BLACK);
    draw_text(BTN_WIPE.x  + 30, BTN_WIPE.y  + 9, "WIPE LOGS >", C_DIM, C_BLACK);
    draw_text(BTN_STOP.x  + 113, BTN_STOP.y + 9, "STOP RADIOS", C_DIM, C_BLACK);

    field_init(&F_PHASEB, BTN_PHASE.x + 8, BTN_PHASE.y + 10, 22, 0, C_CYAN,  C_BLACK);
    field_init(&F_CALB,   BTN_CAL.x   + 4, BTN_CAL.y   + 4,  23, 0, C_AMBER, C_BLACK);
    field_init(&F_SCAL,   BTN_CAL.x   + 4, BTN_CAL.y   + 16, 23, 0, C_DIM,   C_BLACK);
    field_init(&F_S5,     8,  58, 52, 0, C_GREEN, C_BLACK);
    field_init(&F_SWIFI,  8,  70, 52, 0, C_WHITE, C_BLACK);
    field_init(&F_SBLE,   8,  82, 52, 0, C_WHITE, C_BLACK);
    field_init(&F_SSD,    8,  94, 52, 0, C_GREEN, C_BLACK);
    field_init(&F_SGATE,  8, 106, 52, 0, C_WHITE, C_BLACK);
    field_init(&F_SIMP,   8, 118, 52, 0, C_GREEN, C_BLACK);
    field_init(&F_STOUCH, 8, 130, 52, 0, C_DIM,   C_BLACK);
    field_init(&F_SQUIET, BTN_IMPORT.x + 10, BTN_IMPORT.y + 9, 22, 0, C_DIM, C_BLACK);
    draw_tabbar();
}

void draw_setup(uint32_t now)
{
    (void)now;
    char b[60];

    field_set(&F_PHASEB, baseline_phase() == PHASE_CORRIDOR ? "PHASE: CORRIDOR"
                       : baseline_phase() == PHASE_ROOM     ? "PHASE: ROOM"
                                                            : "BASELINE: START");
    field_set(&F_CALB, (s_cal == CAL_IDLE) ? "RSSI CAL" : cal_prompt());
    if (s_cal == CAL_NEAR || s_cal == CAL_CONTACT) {
        char cb[32];
        snprintf(cb, sizeof(cb), "%u/40 samples", (unsigned)s_cal_n);
        field_set_col(&F_SCAL, cb, s_cal_n >= 40 ? C_GREEN : C_AMBER);
    } else {
        field_set_col(&F_SCAL, cal_prompt(), s_cal == CAL_FAILED ? C_RED : C_DIM);
    }

    snprintf(b, sizeof(b), "5GHz usable %u ch %s", (unsigned)s_n_ch5,
             s_n_ch5 == 0 ? "NONE" : (s_dfs_ok ? "incl DFS" : "no DFS 52-144"));
    field_set_col(&F_S5, b, s_n_ch5 ? (s_dfs_ok ? C_GREEN : C_AMBER) : C_RED);

    snprintf(b, sizeof(b), "WiFi frames %lu drop %lu  ident %lu drop %lu",
             (unsigned long)s_frames, (unsigned long)s_frames_drop,
             (unsigned long)s_ie_seen, (unsigned long)s_ie_drop);
    field_set(&F_SWIFI, b);

    snprintf(b, sizeof(b), "BLE adverts %lu  with TX ref %lu",
             (unsigned long)s_adv, (unsigned long)s_with_ref);
    field_set(&F_SBLE, b);

    snprintf(b, sizeof(b), "SD %s", s_sd_ok ? s_fname : "not mounted");
    field_set_col(&F_SSD, b, s_sd_ok ? C_GREEN : C_AMBER);

    {
        char pv[BAND_COUNT];
        for (int i = 0; i < BAND_COUNT; i++)
            pv[i] = g_cal.calibrated[i] ? 'M'
                  : (g_cal.derived[i]   ? 'D' : '-');
        snprintf(b, sizeof(b), "gate 2.4 %d/%d%c  5 %d/%d%c  BLE %d/%d%c",
                 g_cal.near_thresh[BAND_24], g_cal.contact_thresh[BAND_24], pv[BAND_24],
                 g_cal.near_thresh[BAND_5],  g_cal.contact_thresh[BAND_5],  pv[BAND_5],
                 g_cal.near_thresh[BAND_BLE],g_cal.contact_thresh[BAND_BLE],pv[BAND_BLE]);
        field_set(&F_SGATE, b);
    }

    snprintf(b, sizeof(b), "import %lu oui / %lu rules / %lu names",
             (unsigned long)g_oui_n, (unsigned long)g_rules_n,
             (unsigned long)g_namesig_n);
    field_set_col(&F_SIMP, b, g_oui_n ? C_GREEN : C_AMBER);

    // The per-band M/D/- markers on the gate line above need a legend, and
    // the old "RSSI cal/DEFAULT" flag here is now redundant with them.
    snprintf(b, sizeof(b), "M=MEAS D=DERIVED -=DEFAULT  touch %s  col %s",
             g_tcal.valid ? "cal" : "DEF",
             g_disp.confirmed ? (g_disp.bgr ? "BGR" : "RGB") : "UNCHECKED");
    field_set_col(&F_STOUCH, b, g_tcal.valid ? C_DIM : C_AMBER);

    // The import button doubles as its own status: a count means the card
    // was read, zero means it was not.
    if (g_imp_running)  snprintf(b, sizeof(b), "IMPORTING %lu", (unsigned long)g_imp_kept);
    else if (g_oui_n)   snprintf(b, sizeof(b), "RE-IMPORT (%lu OUI)", (unsigned long)g_oui_n);
    else                snprintf(b, sizeof(b), "IMPORT OUI: NONE");
    field_set_col(&F_SQUIET, b, g_oui_n ? C_DIM : C_AMBER);
}

// ===========================================================================
// touch routing
// ===========================================================================
void handle_touch(uint16_t x, uint16_t y, uint32_t now)
{
    if (s_screen == SCR_WELCOME) {
        if      (hit(&BTN_ALL,  x, y)) { sched_set_mode(MODE_SWEEP_ALL);  ui_set_screen(SCR_SWEEP); }
        else if (hit(&BTN_WIFI, x, y)) { sched_set_mode(MODE_WIFI_SWEEP); ui_set_screen(SCR_SWEEP); }
        else if (hit(&BTN_BLE,  x, y)) { sched_set_mode(MODE_BLE_SWEEP);  ui_set_screen(SCR_SWEEP); }
        else if (hit(&BTN_WATCH,x, y)) { sched_set_mode(MODE_WATCH);      ui_set_screen(SCR_SWEEP); }
        return;
    }

    // Procedure pages have no tab bar: BACK and START are the only exits, so
    // a stray tap cannot abandon a half-read explanation.
    if (s_screen == SCR_BASELINE_INFO || s_screen == SCR_RSSICAL_INFO ||
        s_screen == SCR_WIPE_INFO) {
        if (hit(&BTN_INFO_BACK, x, y)) { ui_set_screen(SCR_SETUP); return; }
        if (s_screen == SCR_RSSICAL_INFO && s_cal == CAL_IDLE) {
            rband_t pick = BAND_COUNT;
            if      (hit(&BTN_CALB24, x, y)) pick = BAND_24;
            else if (hit(&BTN_CALB5,  x, y)) pick = BAND_5;
            else if (hit(&BTN_CALBLE, x, y)) pick = BAND_BLE;
            if (pick != BAND_COUNT) {
                cal_begin(pick);
                s_screen_drawn = 0xFF;      // relabel the action button
                return;
            }
        }
        if (hit(&BTN_INFO_START, x, y)) {
            if (s_screen == SCR_WIPE_INFO) {
                uint16_t n = log_wipe();
                fill_screen(C_BLACK);
                char m[48];
                snprintf(m, sizeof(m), "WIPED %u FILE%s", (unsigned)n,
                         n == 1 ? "" : "S");
                draw_text_12(8, 100, m, C_GREEN, C_BLACK);
                draw_text(8, 126, s_sd_ok ? s_fname : "SD not mounted",
                          C_DIM, C_BLACK);
                delay(1800);
                ui_set_screen(SCR_SETUP);
                return;
            }
            if (s_screen == SCR_BASELINE_INFO) {
                phase_t ph = baseline_phase();
                if (ph == PHASE_FREE) {
                    baseline_set_phase(PHASE_CORRIDOR, now);
                    // Straight to SWEEP: the countdown lives there and the
                    // operator is about to walk out of the room.
                    ui_set_screen(SCR_SWEEP);
                } else if (ph == PHASE_CORRIDOR) {
                    baseline_set_phase(PHASE_ROOM, now);
                    ui_set_screen(SCR_SWEEP);
                } else {
                    baseline_apply_diff();
                    baseline_set_phase(PHASE_FREE, now);
                    log_snapshot(now, "BASELINE");
                    // The diff is the whole payoff, so land on the list where
                    // the newly promoted devices have sorted to the top.
                    s_list_top = 0;
                    s_last_sort = 0;
                    ui_set_screen(SCR_LIST);
                }
            } else {
                cal_advance();
                // Committing returns to CAL_IDLE; anything else keeps the
                // page open so the sample count stays visible.
                if (s_cal == CAL_IDLE) ui_set_screen(SCR_SETUP);
                else                   s_screen_drawn = 0xFF;   // relabel
            }
        }
        return;
    }

    if (y >= TABBAR_Y) {
        int tw = SCR_W / SCR_TAB_COUNT;
        int tab = x / tw;
        if (tab >= 0 && tab < SCR_TAB_COUNT) {
            // HOME stops the radios. The welcome screen is the
            // nothing-running state by design, so arriving there with a scan
            // still live was inconsistent - and it meant a sweep could be
            // left running indefinitely by accident.
            if ((screen_t)tab == SCR_WELCOME && s_mode != MODE_IDLE)
                sched_set_mode(MODE_IDLE);
            ui_set_screen((screen_t)tab);
        }
        return;
    }

    if (s_screen == SCR_SWEEP) {
        if (hit(&BTN_SWSTOP, x, y)) {
            sched_set_mode(MODE_IDLE);
            ui_set_screen(SCR_WELCOME);
            return;
        }
        if (s_pass_prompt) { s_pass_prompt = false; return; }
        if (s_mode == MODE_SWEEP_ALL && s_all_phase == 0 &&
            hit(&BTN_NEXTPASS, x, y)) { sched_all_next_pass(now); ui_set_screen(SCR_SWEEP); return; }
        // Anywhere else in the body marks a position. Deliberately a huge
        // target: you are holding this at arm's length behind a headboard.
        sched_mark_position(now);
    } else if (s_screen == SCR_LIST) {
        uint16_t max_top = (s_lview_n > LIST_ROWS)
                         ? (uint16_t)(s_lview_n - LIST_ROWS) : 0;

        if (hit(&BTN_LUP, x, y)) {
            s_list_top = (s_list_top > SCROLL_STEP)
                       ? (uint16_t)(s_list_top - SCROLL_STEP) : 0;
            return;
        }
        if (hit(&BTN_LDN, x, y)) {
            s_list_top = (uint16_t)(s_list_top + SCROLL_STEP);
            if (s_list_top > max_top) s_list_top = max_top;
            return;
        }
        // Header toggles the in-room filter. Scrolling is on the arrows, so
        // the header is free for the one control that changes what the list
        // actually means.
        if (y < LIST_Y0) {
            s_show_all = !s_show_all;
            s_list_top = 0;
            s_last_sort = 0;
            return;
        }

        // Every row is now selectable - the last one is no longer stolen for
        // paging, which is what the scroll column is for.
        int row = (y - LIST_Y0) / ROW_H;
        if (row >= LIST_ROWS) row = LIST_ROWS - 1;
        uint16_t idx = (uint16_t)(s_list_top + row);
        if (idx < s_lview_n) {
            memcpy(s_sel_mac, s_lview[idx]->mac, 6);
            s_sel_band  = s_lview[idx]->band;
            s_sel_valid = true;
            ui_set_screen(SCR_DETAIL);
        }
    } else if (s_screen == SCR_SETUP) {
        // Both open their procedure page, at every stage rather than only
        // from a standing start: mid-procedure is exactly when it matters
        // what the next tap will do, and the page shows live progress.
        if (hit(&BTN_PHASE, x, y)) {
            ui_set_screen(SCR_BASELINE_INFO);
            return;
        } else if (hit(&BTN_CAL, x, y)) {
            ui_set_screen(SCR_RSSICAL_INFO);
            return;
        } else if (hit(&BTN_TCAL2, x, y)) {
            if (!touch_calibrate_interactive()) touch_calibrate_interactive();
            // Re-ask the colour question too: both are "does the panel behave
            // the way the firmware assumes", and they are easiest to check
            // together.
            lcd_confirm_colour_order();
            ui_set_screen(SCR_SETUP);
        } else if (hit(&BTN_RESET, x, y)) {
            tracks_reset();
            s_sel_valid = false;
            s_positions = 0;
            s_lview_n = 0; s_last_sort = 0;
            sched_mark_position(now);
        } else if (hit(&BTN_IMPORT, x, y)) {
            // Force a full CSV reparse: the operator taps this after editing
            // the card, so the cached binary must be discarded.
            fill_screen(C_BLACK);
            draw_text_12(8, 90, "IMPORTING", C_CYAN, C_BLACK);
            draw_text(8, 116, "Reading oui.csv, rules.csv, names.csv...",
                      C_DIM, C_BLACK);
            import_write_templates();
            import_run(true);
            char m[56];
            snprintf(m, sizeof(m), "%lu OUI  %lu rules  %lu names",
                     (unsigned long)g_oui_n, (unsigned long)g_rules_n,
                     (unsigned long)g_namesig_n);
            draw_text(8, 140, m, g_oui_n ? C_GREEN : C_RED, C_BLACK);
            draw_text(8, 154, g_imp_stage, C_DIM, C_BLACK);
            delay(2200);
            ui_set_screen(SCR_SETUP);
            return;
        } else if (hit(&BTN_WIPE, x, y)) {
            ui_set_screen(SCR_WIPE_INFO);
            return;
        } else if (hit(&BTN_STOP, x, y)) {
            sched_set_mode(MODE_IDLE);
            ui_set_screen(SCR_WELCOME);
        }
    }
}

void ui_tick(uint32_t now)
{
    uint16_t tx, ty;
    if (touch_read(&tx, &ty) && (uint32_t)(now - s_last_touch) > 260) {
        s_last_touch = now;
        // A touch always wakes the screen: quiet mode must never be a state
        // the operator cannot get out of.
        if (s_quiet) { s_quiet = false; digitalWrite(PIN_BL, HIGH); ui_set_screen(s_screen); }
        else handle_touch(tx, ty, now);
    }
    if (s_quiet) return;                 // backlight off: no SPI traffic

    // Chrome first, once per screen entry.
    if (s_screen_drawn != (uint8_t)s_screen) {
        switch (s_screen) {
            case SCR_WELCOME: draw_welcome_chrome(); break;
            case SCR_SWEEP:   draw_sweep_chrome();   break;
            case SCR_LIST:    draw_list_chrome();    break;
            case SCR_DETAIL:  draw_detail_chrome();  break;
            case SCR_BASELINE_INFO: draw_info_page(&INFO_BASELINE); break;
            case SCR_RSSICAL_INFO:  draw_info_page(&INFO_RSSICAL);  break;
            default:          draw_setup_chrome();   break;
        }
        s_screen_drawn = (uint8_t)s_screen;
        s_last_draw = 0;
    }

    if (s_last_draw && (uint32_t)(now - s_last_draw) < REDRAW_MS) return;
    s_last_draw = now;

    switch (s_screen) {
        case SCR_WELCOME:                 // fully static, nothing to refresh
        case SCR_BASELINE_INFO: draw_info_live(&INFO_BASELINE, now); break;
        case SCR_RSSICAL_INFO:  draw_info_live(&INFO_RSSICAL,  now); break;
        case SCR_WIPE_INFO:     draw_info_live(&INFO_WIPE,     now); break;
        case SCR_SWEEP:   draw_sweep(now);  break;
        case SCR_LIST:    draw_list(now);   break;
        case SCR_DETAIL:  draw_detail(now); break;
        default:          draw_setup(now);  break;
    }
}
