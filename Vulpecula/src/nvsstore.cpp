// nvsstore.cpp - persistence for the calibration data.
//
// Split out of proximity.cpp so the gate itself stays hardware-free. That is
// what lets test/gate_test.cpp include the real proximity.cpp instead of a
// copy sliced out of the sketch with string markers, which is how it used to
// work and how it used to drift.
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

void prox_cal_save()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_KEY, &g_cal, sizeof(prox_cal_t));
    nvs_commit(h);
    nvs_close(h);
}

void prox_init()
{
    prox_cal_reset_defaults();
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(prox_cal_t);
    prox_cal_t st;
    if (nvs_get_blob(h, NVS_KEY, &st, &len) == ESP_OK && len == sizeof(st)) {
        // Sanity-check before trusting a stored blob. A corrupt value that
        // widened the gate would silently turn this into a room-scale scanner,
        // which is the exact failure mode the design exists to avoid.
        bool sane = st.pathloss_n > 1.0f && st.pathloss_n < 4.0f;
        for (int b = 0; b < BAND_COUNT && sane; b++) {
            if (st.near_thresh[b] > -10 || st.near_thresh[b] < -90) sane = false;
            if (st.contact_thresh[b] <= st.near_thresh[b])          sane = false;
        }
        if (sane) g_cal = st;
    }
    nvs_close(h);
}

// ---------------------------------------------------------------------------
// Panel colour order.
//
// Stored separately from the proximity calibration so that adding a field to
// one does not invalidate the other - prox_cal_t is length-checked on load,
// and a size change silently resets it.
// ---------------------------------------------------------------------------
static const char *NVS_DISP_KEY = "disp_cfg";

void disp_cfg_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(disp_cfg_t);
    disp_cfg_t st;
    if (nvs_get_blob(h, NVS_DISP_KEY, &st, &len) == ESP_OK &&
        len == sizeof(st)) {
        g_disp = st;
    }
    nvs_close(h);
}

void disp_cfg_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, NVS_DISP_KEY, &g_disp, sizeof(disp_cfg_t));
    nvs_commit(h);
    nvs_close(h);
}
