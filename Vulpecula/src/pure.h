// pure.h - the hardware-free core.
//
// Everything declared here compiles on a host with no Arduino headers, no
// stubs and no board. That is what lets the gate test, the triage test and
// the parser fuzzer include the real implementation rather than a copy
// sliced out of the sketch with string markers.
//
// If you add something here that needs Arduino.h, it belongs in vulpecula.h
// instead.
#pragma once
#include "config.h"
#include "types.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <strings.h>
#include <math.h>

// --- proximity.cpp ---
void prox_cal_reset_defaults();
void prox_cal_save();
void prox_init();
void ring_reset(rssi_ring_t *r);
void ring_push(rssi_ring_t *r, int8_t rssi, uint32_t now);
uint8_t ring_window(const rssi_ring_t *r, uint32_t now, uint32_t win,
                           int8_t *out, uint8_t cap);
int8_t ring_peak(const rssi_ring_t *r, uint32_t now, uint32_t win);
int8_t ring_median(const rssi_ring_t *r, uint32_t now, uint32_t win);
uint8_t ring_count_above(const rssi_ring_t *r, uint32_t now,
                                uint32_t win, int8_t th);
ranging_ref_t prox_ref_from_txpower(int8_t tx_dbm);
ranging_ref_t prox_ref_from_ibeacon(int8_t measured_power);
ranging_ref_t prox_ref_from_eddystone(int8_t ranging_power);
float rssi_to_distance(int8_t rssi, int8_t at1m, float n);
int8_t distance_to_rssi(float d, int8_t at1m, float n);
float prox_distance_m(const rssi_ring_t *r, const ranging_ref_t *ref,
                             uint32_t now);
tier_t prox_resolve_tier(const rssi_ring_t *r, rband_t band,
                                const ranging_ref_t *ref, tier_t prev,
                                uint32_t now);
const char * tier_name(tier_t t);
const char * band_name(rband_t b);
uint16_t tier_colour(tier_t t);
extern prox_cal_t g_cal;

// --- vendor.cpp ---
bool istrstr(const char *hay, const char *needle);
bool sig_match_str(const str_sig_t *tbl, uint32_t n, const char *hay,
                          sig_weight_t *w_out);
const char * dtype_name(dtype_t d);
const char * dtier_name(uint8_t t);
bool dtype_is_recorder(dtype_t d);
const vendor_sig_t * vendor_lookup(const uint8_t mac[6]);
void dtype_claim(track_t *t, dtype_t d, uint8_t tier);
dtype_t wps_category_to_dtype(uint16_t cat, uint16_t sub, const char **detail);
dtype_t appearance_to_dtype(uint16_t appear);
dtype_t svc_uuid_to_dtype(uint16_t u);
extern const str_sig_t SIG_SSID[];
extern const str_sig_t SIG_BLE_CAM[];
extern const str_sig_t SIG_BLE_MIC[];
extern const oui_sig_t SIG_OUI[];
extern const uint16_t SIG_SVC16[];
extern const uint16_t SIG_COMPANY[];
extern const vendor_sig_t VENDOR_OUI[];

// --- parsers.cpp ---
void copy_printable(char *dst, size_t cap, const uint8_t *src, uint16_t n);
void parse_wps(track_t *t, const uint8_t *d, uint16_t dlen);
void parse_mgmt_ies(track_t *t, const uint8_t *f, uint16_t len);
bool parse_ble_adv(track_t *t, const uint8_t *adv, uint8_t adv_len);
void str_lower(char *s);
dtype_t dtype_from_text(const char *s);
uint8_t weight_from_text(const char *s);
int csv_split(char *line, char **out, int maxf);
bool hex3(const char *s, uint8_t out[3]);
void rules_apply(const char *vendor, uint8_t *dtype, uint8_t *weight);
const oui_rec_t * oui_db_lookup(const uint8_t mac[6]);
bool namesig_match(const char *name, bool is_ble,
                          uint8_t *dtype, uint8_t *weight);
int cmp_oui(const void *a, const void *b);
extern const uint8_t WPS_OUI_TYPE[4];

// --- triage.cpp ---
const char * risk_name(risk_t r);
uint16_t risk_colour(risk_t r);
risk_t triage_eval(const track_t *t, const char **why);
void triage_update(track_t *t, uint32_t now);

// --- classify.cpp ---
int score_for(uint32_t bit);
bool looks_like_streaming(const track_t *t);
void classify_track(track_t *t);
confidence_t classify_confidence(const track_t *t);
const char * confidence_name(confidence_t c);
uint8_t classify_explain(const track_t *t, char lines[][44], uint8_t maxl);

// --- cross-module objects the generator could not infer ---
// Multi-declarator and sizeof-macro forms, plus the imported tables that
// parsers.cpp reads and importdb.cpp fills.
extern const str_sig_t SIG_SSID[];
extern const str_sig_t SIG_BLE_CAM[];
extern const str_sig_t SIG_BLE_MIC[];
extern const uint32_t N_SIG_SSID, N_SIG_BLE_CAM, N_SIG_BLE_MIC;
extern vrule_t  g_rules[RULES_MAX];
extern uint32_t g_rules_n;
extern oui_rec_t *g_oui_db;
extern uint32_t   g_oui_n;
extern namesig_t *g_namesig;
extern uint32_t   g_namesig_n;
// BLE UUID and company tables, with their counts published for classify.cpp.
extern const uint16_t SIG_SVC16[];
extern const uint16_t SIG_COMPANY[];
extern const uint32_t N_SIG_SVC16, N_SIG_COMPANY;
