// wifi_cap.cpp - Passive dual-band 802.11 capture.
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
// WIFI CAPTURE  -  passive dual-band 802.11
//
// Receive only. No probe requests, no association, no deauth. That keeps the
// tool invisible to whoever placed the device and keeps it uncomplicated in
// the jurisdictions where transmitting is the line that matters.
// ===========================================================================

// (type definitions hoisted to the TYPE DEFINITIONS block near the top -
//  see the note there before moving them back)

// ---------------------------------------------------------------------------
// Management-frame payload queue.
//
// The main capture queue carries only the first 36 bytes of each frame, which
// is all that RSSI, addressing and traffic accounting need, and keeps the
// queue small enough to absorb a data-frame burst.
//
// But everything interesting about a device's IDENTITY lives past byte 36:
// tagged parameters start at exactly offset 36, so the SSID element begins
// there and the WPS information element is further in still. The first build
// tried to read the SSID out of the 36-byte buffer and therefore never
// returned one - not truncated, never parsed at all, for any frame length.
// The entire SoftAP-camera SSID signature path was dead as a result.
//
// So beacons and probe responses get a second, larger copy on their own
// queue. It is short and drops when full on purpose: beacons repeat every
// ~100 ms, so every access point is captured within a channel dwell or two,
// and a dropped copy costs nothing. Data frames never enter this queue.
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t  mac[6];
    uint8_t  band;
    uint16_t len;
    uint8_t  buf[IE_BUF_LEN];
} ie_item_t;

QueueHandle_t s_iq = NULL;
volatile uint32_t s_ie_seen = 0, s_ie_drop = 0;

void ie_pump(uint32_t now)
{
    (void)now;
    if (!s_iq) return;
    static ie_item_t it;                 // 330 bytes: static, not on the stack
    for (int n = 0; n < 4; n++) {        // a few per tick; parsing is not urgent
        if (xQueueReceive(s_iq, &it, 0) != pdTRUE) break;
        track_t *t = track_get(it.mac, (rband_t)it.band, false);
        if (!t) continue;                // gone already, nothing to attach to
        parse_mgmt_ies(t, it.buf, it.len);
    }
}


QueueHandle_t s_wq = NULL;
volatile uint32_t s_frames = 0, s_frames_drop = 0;
rband_t s_band = BAND_24;
uint8_t s_channel = 1;
volatile bool s_wifi_run = false;
uint8_t s_ch5[sizeof(CH_5_CANDIDATES)];
uint8_t s_n_ch5 = 0;
bool    s_dfs_ok = false;

#define FC_TYPE(fc)     (((fc) >> 2) & 0x03)
#define FC_SUBTYPE(fc)  (((fc) >> 4) & 0x0F)
#define FC_TO_DS(f2)    ((f2) & 0x01)
#define FC_FROM_DS(f2)  (((f2) >> 1) & 0x01)

const uint8_t BCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// The promiscuous callback runs inside the WiFi driver task, so it must post
// and return. Anything heavier stalls the radio and costs us frames.
void promisc_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!s_wifi_run || !s_wq) return;
    if (type == WIFI_PKT_MISC) return;

    const wifi_promiscuous_pkt_t *p = (const wifi_promiscuous_pkt_t *)buf;
    s_frames++;

    cap_item_t it;
    uint16_t copy = p->rx_ctrl.sig_len;
    if (copy > sizeof(it.hdr)) copy = sizeof(it.hdr);
    memcpy(it.hdr, p->payload, copy);
    if (copy < sizeof(it.hdr)) memset(it.hdr + copy, 0, sizeof(it.hdr) - copy);
    it.len     = p->rx_ctrl.sig_len;
    it.rssi    = (int8_t)p->rx_ctrl.rssi;
    it.channel = s_channel;
    it.band    = (uint8_t)s_band;

    if (xQueueSend(s_wq, &it, 0) != pdTRUE) s_frames_drop++;

    // Beacons and probe responses carry the device's own identity past byte
    // 36 - SSID, and often WPS manufacturer, model and declared device type.
    // Copy those to the IE queue. Dropping when full is fine: beacons repeat
    // every ~100 ms, so nothing is permanently lost.
    uint8_t type_ = FC_TYPE(it.hdr[0]), sub_ = FC_SUBTYPE(it.hdr[0]);
    if (type_ == TYPE_MGMT && (sub_ == ST_BEACON || sub_ == ST_PROBE_RESP) &&
        s_iq && p->rx_ctrl.sig_len > 36) {
        static ie_item_t ie;            // driver task, so not on the stack
        memcpy(ie.mac, p->payload + 10, 6);           // addr2, the transmitter
        ie.band = (uint8_t)s_band;
        uint16_t n = p->rx_ctrl.sig_len;
        if (n > IE_BUF_LEN) n = IE_BUF_LEN;
        memcpy(ie.buf, p->payload, n);
        ie.len = n;
        s_ie_seen++;
        if (xQueueSend(s_iq, &ie, 0) != pdTRUE) s_ie_drop++;
    }
}

// Locally-administered, unicast, random. Regenerated every boot, so there is
// no identifier that persists across sessions.
//
// BLE needs no equivalent: the scan is passive, so the controller transmits
// nothing at all and has no address to leak. If active scanning is ever added,
// this has to be revisited - a scan request carries the scanner's address.
static void randomise_own_mac()
{
    uint8_t mac[6];
    esp_fill_random(mac, sizeof(mac));
    mac[0] = (uint8_t)((mac[0] & 0xFE) | 0x02);   // unicast + locally administered
    if (esp_wifi_set_mac(WIFI_IF_STA, mac) == ESP_OK) {
        Serial.printf("own MAC randomised to %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        // Worth saying out loud rather than failing quietly: the operator is
        // then carrying a trackable device.
        Serial.println("WARNING: could not randomise own MAC - this board is "
                       "traceable across sessions");
    }
}

void wifi_cap_init()
{
    if (!s_wq) s_wq = xQueueCreate(CAP_QUEUE_LEN, sizeof(cap_item_t));
    if (!s_iq) s_iq = xQueueCreate(IE_QUEUE_LEN, sizeof(ie_item_t));
    WiFi.mode(WIFI_MODE_STA);
    WiFi.disconnect(true, false);
    randomise_own_mac();
    esp_wifi_set_ps(WIFI_PS_NONE);      // power save would gate our RX

    wifi_promiscuous_filter_t f = {};
    // Management for beacons/probes, data for the streaming heuristic.
    // Control frames are high-volume and carry no usable addr2 identity, so
    // filtering them keeps the queue for frames we can act on.
    f.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(&promisc_cb);
}

bool wifi_cap_set_band(rband_t band)
{
    if (band == s_band) return true;
    if (band != BAND_24 && band != BAND_5) return false;
    // NOTE: the band-mode API arrived with C5 support. If your core exposes a
    // different symbol, THIS is the one call to adjust.
    wifi_band_mode_t m = (band == BAND_5) ? WIFI_BAND_MODE_5G_ONLY
                                          : WIFI_BAND_MODE_2G_ONLY;
    if (esp_wifi_set_band_mode(m) != ESP_OK) return false;
    s_band = band;
    vTaskDelay(pdMS_TO_TICKS(BAND_SWITCH_SETTLE_MS));
    return true;
}

bool wifi_cap_set_channel(uint8_t ch)
{
    if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    s_channel = ch;
    return true;
}

// Probe which 5 GHz channels the driver will actually accept, rather than
// assuming. The C5 supports DFS channels but only PASSIVE radar detection, so
// the regulatory table may refuse 52-144 depending on country config. We
// measure once at boot and report it on the SETUP screen.
void wifi_cap_probe_5g()
{
    s_n_ch5 = 0; s_dfs_ok = false;
    bool was = s_wifi_run;
    s_wifi_run = false;                 // ignore frames while we thrash

    if (!wifi_cap_set_band(BAND_5)) { s_wifi_run = was; return; }

    for (uint32_t i = 0; i < sizeof(CH_5_CANDIDATES); i++) {
        uint8_t ch = CH_5_CANDIDATES[i];
        if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
            s_ch5[s_n_ch5++] = ch;
            if (ch >= 52 && ch <= 144) s_dfs_ok = true;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    wifi_cap_set_band(BAND_24);
    wifi_cap_set_channel(CH_24[0]);
    s_wifi_run = was;
}

void wifi_cap_start() { esp_wifi_set_promiscuous(true);  s_wifi_run = true; }
void wifi_cap_stop()
{
    s_wifi_run = false;
    esp_wifi_set_promiscuous(false);
    // Drain, or a stale burst lands in the next pass tagged with the wrong
    // band and channel.
    if (s_wq) xQueueReset(s_wq);
    if (s_iq) xQueueReset(s_iq);
}

// SSID and WPS parsing now happen in parse_mgmt_ies() against the full
// payload on the IE queue. The old parse_ssid() that lived here read from the
// 36-byte header copy, where the tagged parameters have not started yet, and
// so returned nothing for every frame at every length.

void handle_frame(const cap_item_t *it, uint32_t now)
{
    const uint8_t *f = it->hdr;
    uint8_t fc1 = f[0], fc2 = f[1];
    uint8_t type = FC_TYPE(fc1), sub = FC_SUBTYPE(fc1);
    const uint8_t *addr1 = f + 4, *addr2 = f + 10;
    rband_t band = (rband_t)it->band;

    // RSSI belongs to the radio that TRANSMITTED, which is addr2 and only
    // addr2. See the attribution rule on track_observe_rssi.
    track_t *tx = NULL;
    if (memcmp(addr2, BCAST, 6) != 0) {
        tx = track_get(addr2, band, true);
        if (tx) { tx->channel = it->channel; track_observe_rssi(tx, it->rssi, now); }
    }

    if (type == TYPE_MGMT) {
        if (!tx) return;
        if (sub == ST_BEACON || sub == ST_PROBE_RESP) {
            tx->is_ap = true;
            if (sub == ST_BEACON) tx->n_beacon++; else tx->n_probe_resp++;
            // Identity is filled in by ie_pump() from the full payload.
        } else if (sub == ST_PROBE_REQ) {
            tx->n_probe_req++;
            // A probe request's SSID element sits at offset 24, inside the
            // header copy we do have. Zero length means a broadcast probe:
            // a device hunting for any network it knows, common on
            // unprovisioned cameras.
            if (it->len > 25 && f[24] == IE_SSID && f[25] == 0)
                tx->evidence |= E_WILDCARD_PROBE;
        } else tx->n_mgmt_other++;
        return;
    }

    if (type == TYPE_DATA) {
        bool to_ds = FC_TO_DS(fc2), from_ds = FC_FROM_DS(fc2);
        if (to_ds && !from_ds) {
            // Station -> AP. addr2 is the station: this is the uplink that
            // drives the streaming heuristic.
            if (tx) { tx->n_data_up++; tx->bytes_up += it->len; }
        } else if (!to_ds && from_ds) {
            // AP -> station. addr2 is the AP (correctly gets the RSSI) and
            // addr1 is the station. Credit the station's downlink bytes
            // WITHOUT touching its RSSI - this is how we notice a camera that
            // is currently only receiving. Traffic shape, never proximity.
            if (tx) tx->is_ap = true;
            if (memcmp(addr1, BCAST, 6) != 0) {
                track_t *rx = track_get(addr1, band, true);
                if (rx) {
                    rx->n_data_down++;
                    rx->bytes_down += it->len;
                    rx->last_ms = now;
                    if (rx->first_ms == 0) rx->first_ms = now;
                }
            }
        } else if (!to_ds && !from_ds) {
            if (tx) { tx->n_data_up++; tx->bytes_up += it->len; }
        }
    }
}

void wifi_cap_pump(uint32_t now)
{
    if (!s_wq) return;
    cap_item_t it;
    for (int i = 0; i < 64; i++) {       // bounded so the UI still gets time
        if (xQueueReceive(s_wq, &it, 0) != pdTRUE) break;
        handle_frame(&it, now);
    }
}
