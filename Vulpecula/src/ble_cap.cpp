// ble_cap.cpp - Passive BLE advertisement capture.
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
// BLE CAPTURE  -  passive only
//
// Passive, not active. Active scanning would fetch scan responses (more name
// and UUID data) but it transmits a scan request, making the tool visible.
// Not worth it.
//
// The important job is extracting a calibrated transmit-power reference where
// one exists, because consumer BLE TX power spans ~28 dB and that uncertainty
// otherwise swamps the wall margin the gate depends on.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

QueueHandle_t s_bq = NULL;
volatile uint32_t s_adv = 0, s_adv_drop = 0;
uint32_t s_with_ref = 0;
volatile bool s_ble_run = false;
static NimBLEScan *s_scan = NULL;


class SweepScanCB : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        if (!s_ble_run || !s_bq) return;
        s_adv++;
        ble_item_t it;
        memset(&it, 0, sizeof(it));

        const uint8_t *a = dev->getAddress().getBase()->val;
        // NimBLE stores addresses little-endian; present big-endian so it
        // matches every other tool.
        for (int i = 0; i < 6; i++) it.mac[i] = a[5 - i];
        it.addr_type = dev->getAddress().getType();
        it.rssi = (int8_t)dev->getRSSI();

        // Copy the raw payload and walk it ourselves. NimBLE's accessors are
        // convenient but hide TX power and the beacon ranging fields, which
        // are exactly what we need most.
        std::vector<uint8_t> pl = dev->getPayload();
        size_t n = pl.size();
        if (n > sizeof(it.adv)) n = sizeof(it.adv);
        if (n) memcpy(it.adv, pl.data(), n);
        it.adv_len = (uint8_t)n;

        if (xQueueSend(s_bq, &it, 0) != pdTRUE) s_adv_drop++;
    }
};
SweepScanCB s_cb;

void ble_cap_init()
{
    if (!s_bq) s_bq = xQueueCreate(BLE_QUEUE_LEN, sizeof(ble_item_t));
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_N12);   // we never advertise; stay quiet
    s_scan = NimBLEDevice::getScan();
    s_scan->setScanCallbacks(&s_cb, false);
    s_scan->setActiveScan(false);              // PASSIVE - never transmit
    // Window == interval means continuous listening, which is the whole point
    // of a BLE-exclusive pass: a 10.24 s advertiser needs every millisecond.
    s_scan->setInterval(160);                  // 160 * 0.625 ms = 100 ms
    s_scan->setWindow(160);
    s_scan->setDuplicateFilter(false);         // we need every advert for RSSI
    s_scan->setMaxResults(0);                  // stream via callback
}

void ble_cap_start()
{
    if (!s_scan) return;
    s_ble_run = true;
    s_scan->start(0, false, true);             // 0 = continuous
}
void ble_cap_stop()
{
    s_ble_run = false;
    if (s_scan) s_scan->stop();
    if (s_bq) xQueueReset(s_bq);
}

void handle_advert(const ble_item_t *it, uint32_t now)
{
    track_t *t = track_get(it->mac, BAND_BLE, true);
    if (!t) return;
    t->addr_type = it->addr_type;
    track_observe_rssi(t, it->rssi, now);

    if (parse_ble_adv(t, it->adv, it->adv_len)) s_with_ref++;
}

void ble_cap_pump(uint32_t now)
{
    if (!s_bq) return;
    ble_item_t it;
    for (int i = 0; i < 48; i++) {
        if (xQueueReceive(s_bq, &it, 0) != pdTRUE) break;
        handle_advert(&it, now);
    }
}
