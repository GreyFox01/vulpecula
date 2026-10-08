#pragma once
#include <stddef.h>
#include <esp_wifi.h>
typedef int nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
static inline esp_err_t nvs_open(const char*,nvs_open_mode_t,nvs_handle_t*){return -1;}
static inline esp_err_t nvs_get_blob(nvs_handle_t,const char*,void*,size_t*){return -1;}
static inline esp_err_t nvs_set_blob(nvs_handle_t,const char*,const void*,size_t){return 0;}
static inline esp_err_t nvs_commit(nvs_handle_t){return 0;}
static inline void nvs_close(nvs_handle_t){}
