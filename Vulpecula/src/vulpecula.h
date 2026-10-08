// vulpecula.h - the firmware-wide header.
//
// Includes the hardware-free core (pure.h) and adds everything that touches
// the board. Hardware modules include this; pure modules must not.
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <NimBLEDevice.h>
#include "pure.h"

// --- display.cpp ---
void spi_mux_init();
inline void spi_take();
inline void spi_give();
inline void lcd_cs_low();
inline void lcd_cs_high();
void lcd_cmd(uint8_t c);
void lcd_data(const uint8_t *d, size_t n);
void lcd_cmd_data(uint8_t c, const uint8_t *d, size_t n);
uint8_t madctl_value();
void lcd_init();
void lcd_set_window(int16_t x, int16_t y, int16_t w, int16_t h);
void fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col);
void fill_screen(uint16_t col);
void draw_hline(int16_t x, int16_t y, int16_t w, uint16_t c);
void draw_vline(int16_t x, int16_t y, int16_t h, uint16_t c);
void draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c);
inline char ui_fold(char c);
void draw_char(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg,
                      uint8_t scale);
void draw_text(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg);
void draw_char_12(int16_t x, int16_t y, char ch, uint16_t fg, uint16_t bg);
void draw_text_12(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg);
void draw_text_12_c(int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg);
void field_init(field_t *f, int16_t x, int16_t y, uint8_t w, uint8_t big,
                       uint16_t fg, uint16_t bg);
void field_invalidate(field_t *f);
void field_set_col(field_t *f, const char *s, uint16_t fg);
void field_set(field_t *f, const char *s);
void bar_init(bar_t *b, int16_t x, int16_t y, int16_t w, int16_t h);
void bar_set(bar_t *b, int16_t fill, uint16_t col);
void draw_text_c(int16_t cx, int16_t y, const char *s, uint16_t fg, uint16_t bg);
void lcd_selftest();
extern SemaphoreHandle_t g_spi_mux;
extern bool g_lcd_ok;

// --- touch.cpp ---
void touch_cal_defaults();
void touch_cal_load();
void touch_cal_save();
uint16_t xpt_xfer(uint8_t cmd);
bool touch_raw(uint16_t *rx, uint16_t *ry);
bool touch_read(uint16_t *sx, uint16_t *sy);
bool touch_await_press(uint16_t *rx, uint16_t *ry, uint32_t timeout_ms);
bool touch_calibrate_interactive();
void led_rgb(uint8_t r, uint8_t g, uint8_t b);
extern touch_cal_t g_tcal;
extern volatile uint16_t g_touch_raw_x;
extern const char * TCAL_KEY;

// --- tracks.cpp ---
void tracks_reset();
bool tracks_init();
uint16_t mac_hash(const uint8_t mac[6], rband_t band);
track_t * track_get(const uint8_t mac[6], rband_t band, bool create);
void track_observe_rssi(track_t *t, int8_t rssi, uint32_t now);
void tracks_tick(uint32_t now);
void tracks_close_position(uint32_t now);
void baseline_apply_diff();
phase_t baseline_phase(void);
void baseline_set_phase(phase_t p, uint32_t now);
uint32_t baseline_elapsed_ms(uint32_t now);
int cmp_tracks(const void *pa, const void *pb);
bool track_in_room(const track_t *t);
uint16_t tracks_sorted_filtered(track_t **out, uint16_t cap,
                                       bool all, uint16_t *hidden);
uint16_t tracks_sorted(track_t **out, uint16_t cap);
extern uint16_t g_used;
extern phase_t g_phase;
extern uint32_t g_phase_start;
extern bool s_show_all;

// --- wifi_cap.cpp ---
void ie_pump(uint32_t now);
void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type);
void wifi_cap_init();
bool wifi_cap_set_band(rband_t band);
bool wifi_cap_set_channel(uint8_t ch);
void wifi_cap_probe_5g();
void wifi_cap_start();
void wifi_cap_stop();
void handle_frame(const cap_item_t *it, uint32_t now);
void wifi_cap_pump(uint32_t now);
extern QueueHandle_t s_iq;
extern volatile uint32_t s_ie_seen;
extern QueueHandle_t s_wq;
extern volatile uint32_t s_frames;
extern rband_t s_band;
extern uint8_t s_channel;
extern volatile bool s_wifi_run;
extern uint8_t s_ch5[sizeof(CH_5_CANDIDATES)];
extern uint8_t s_n_ch5;
extern bool s_dfs_ok;
extern const uint8_t BCAST[6];

// --- ble_cap.cpp ---
void ble_cap_init();
void ble_cap_start();
void ble_cap_stop();
void handle_advert(const ble_item_t *it, uint32_t now);
void ble_cap_pump(uint32_t now);
extern QueueHandle_t s_bq;
extern volatile uint32_t s_adv;
extern uint32_t s_with_ref;
extern volatile bool s_ble_run;

// --- sched.cpp ---
const char * sched_mode_name(sweep_mode_t m);
rband_t sched_active_proto();
const char * sched_status();
void sched_mark_position(uint32_t now);
void ensure_5g_probed();
void radios_wifi_only();
void radios_ble_only();
void sched_set_mode(sweep_mode_t m);
void sched_all_next_pass(uint32_t now);
void sched_accrue(uint32_t now);
uint32_t sched_position_radio_ms();
uint32_t sched_covered_interval_ms();
bool sched_sweep_too_fast();
void sched_note_strongest(int8_t peak, uint32_t now);
void wifi_rotate(uint32_t now);
void watch_rotate(uint32_t now);
void sched_tick(uint32_t now);
extern sweep_mode_t s_mode;
extern rband_t s_active;
extern uint8_t s_ch24_idx;
extern uint32_t s_last_hop;
extern uint32_t s_pos_start;
extern int8_t s_last_strongest;
extern uint32_t s_positions;
extern uint8_t s_all_phase;
extern bool s_pass_prompt;
extern uint32_t s_pass_radio_ms;
extern bool s_probed_5g;

// --- calib.cpp ---
int cmp_i8(const void *a, const void *b);
int8_t cal_p80();
void cal_feed(int8_t rssi);
void cal_advance();
const char * cal_prompt();
extern cal_state_t s_cal;
extern rband_t s_cal_band;
extern int8_t s_cal_buf[CAL_MAX_SAMPLES];
extern uint16_t s_cal_n;
extern int8_t s_cal_near;

// --- logsd.cpp ---
void csv_safe(char *dst, size_t cap, const char *src);
void log_pseudonym(const uint8_t mac[6], char *out, size_t cap);
bool log_init();
void log_snapshot(uint32_t now, const char *event);
extern bool s_sd_ok;
extern char s_fname[40];
extern File s_file;
extern uint8_t s_salt[16];

// --- importdb.cpp ---
void rules_load();
void namesig_load();
bool oui_db_alloc();
bool oui_load_bin();
void oui_write_bin();
bool oui_parse_csv();
bool import_run(bool force_reparse);
void import_write_templates();
extern uint32_t g_oui_n;
extern uint32_t g_namesig_n;
extern volatile uint32_t g_imp_lines;
extern const char * g_imp_stage;
extern bool g_imp_running;
extern const char * F_OUI_CSV;
extern const char * F_OUI_BIN;
extern const char * F_RULES_CSV;
extern const char * F_NAMES_CSV;
extern vrule_t g_rules[RULES_MAX];
extern uint32_t g_rules_n;

// --- ui.cpp ---
track_t * sel_track();
void ui_set_screen(screen_t s);
bool hit(const rect_t *r, uint16_t x, uint16_t y);
void draw_button(const rect_t *r, const char *label, const char *sub,
                        uint16_t frame, uint16_t label_col);
void draw_menu_item(const rect_t *r, const char *label, const char *sub,
                           uint16_t fill);
void draw_welcome_chrome();
void draw_tabbar();
void draw_sweep_chrome();
void draw_sweep(uint32_t now);
void draw_tri(int16_t cx, int16_t cy, int16_t half, bool up, uint16_t c);
void draw_scroll_col(uint16_t n);
void draw_list_chrome();
void draw_list(uint32_t now);
void draw_detail_chrome();
void draw_detail(uint32_t now);
void draw_info_page(const infopage_t *p);
const char * info_action_label(const infopage_t *p);
void draw_info_live(const infopage_t *p, uint32_t now);
void draw_setup_chrome();
void draw_setup(uint32_t now);
void handle_touch(uint16_t x, uint16_t y, uint32_t now);
void ui_tick(uint32_t now);
extern screen_t s_screen;
extern uint8_t s_sel_mac[6];
extern rband_t s_sel_band;
extern bool s_sel_valid;
extern uint32_t s_last_draw;
extern uint16_t s_list_top;
extern bool s_quiet;
extern const rect_t BTN_ALL;
extern const rect_t BTN_WIFI;
extern const rect_t BTN_BLE;
extern const rect_t BTN_WATCH;
extern const char * TAB_NAMES[SCR_TAB_COUNT];
extern const rect_t BTN_NEXTPASS;
extern bar_t B_PEAK;
extern uint16_t s_tier_bg;
extern const rect_t BTN_LUP;
extern const rect_t BTN_LDN;
extern field_t F_LHDR;
extern uint16_t s_row_stripe[LIST_ROWS];
extern uint32_t s_last_sort;
extern uint16_t s_lview_n;
extern uint16_t s_lview_hidden;
extern const rect_t BTN_INFO_BACK;
extern const rect_t BTN_INFO_START;
extern const infopage_t INFO_BASELINE;
extern const rect_t BTN_PHASE;
extern const rect_t BTN_CAL;
extern const rect_t BTN_TCAL2;
extern const rect_t BTN_RESET;
extern const rect_t BTN_IMPORT;
extern const rect_t BTN_STOP;

// --- cross-module objects the generator could not infer ---
extern SPISettings SPI_LCD, SPI_TCH;
extern int8_t   s_cal_near, s_cal_contact;
extern uint16_t s_cal_n;
extern uint32_t s_radio_ms[BAND_COUNT];
extern uint8_t  s_ch5[];
extern volatile uint16_t g_touch_raw_x, g_touch_raw_y, g_touch_raw_z;
extern volatile uint32_t s_frames, s_frames_drop, s_adv, s_adv_drop,
                         s_ie_seen, s_ie_drop;
extern uint32_t s_with_ref;
void prox_init(void);
void prox_cal_save(void);
extern volatile uint32_t g_imp_lines, g_imp_kept;
extern const char *g_imp_stage;
extern bool g_imp_running;
track_t *track_at(uint16_t idx);
uint16_t log_wipe(void);
void cal_derive_5g(void);
int cal_ref_correction(rband_t band);

// --- calibration radio control (added with the band lock) ---
void sched_cal_band(rband_t band);
void sched_cal_channel(uint8_t ch);
void sched_cal_release(void);
void cal_begin(rband_t band);
track_t *cal_find_ref(void);
extern bool s_cal_ref_seen;
extern int8_t s_cal_last_rssi;
extern bool s_band_locked;

// --- panel colour order, chosen at runtime and confirmed by the operator ---
extern disp_cfg_t g_disp;
void lcd_apply_colour_order(void);
bool lcd_confirm_colour_order(void);
void disp_cfg_load(void);
void disp_cfg_save(void);
bool hit(const rect_t *r, uint16_t x, uint16_t y);
