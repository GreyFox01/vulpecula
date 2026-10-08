#pragma once
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NO_FREE_PAGES 1
#define ESP_ERR_NVS_NEW_VERSION_FOUND 2
typedef enum { WIFI_PKT_MGMT, WIFI_PKT_DATA, WIFI_PKT_MISC } wifi_promiscuous_pkt_type_t;
typedef struct { signed rssi:8; unsigned sig_len:12; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[0]; } wifi_promiscuous_pkt_t;
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2
typedef enum { WIFI_BAND_MODE_2G_ONLY, WIFI_BAND_MODE_5G_ONLY, WIFI_BAND_MODE_AUTO } wifi_band_mode_t;
typedef enum { WIFI_SECOND_CHAN_NONE } wifi_second_chan_t;
#define WIFI_PS_NONE 0
typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP } wifi_interface_t;
static inline esp_err_t esp_wifi_set_mac(wifi_interface_t, const uint8_t*){return ESP_OK;}
typedef void (*wifi_promiscuous_cb_t)(void*, wifi_promiscuous_pkt_type_t);
static inline esp_err_t esp_wifi_set_ps(int){return ESP_OK;}
static inline esp_err_t esp_wifi_set_promiscuous(bool){return ESP_OK;}
static inline esp_err_t esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t*){return ESP_OK;}
static inline esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t){return ESP_OK;}
static inline esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t){return ESP_OK;}
static inline esp_err_t esp_wifi_set_channel(uint8_t,wifi_second_chan_t){return ESP_OK;}
