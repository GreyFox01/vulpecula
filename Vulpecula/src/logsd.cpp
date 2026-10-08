// logsd.cpp - SD logging with session-salted MAC pseudonyms.
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
// SD LOGGING
//
// MACs are written as a session-salted hash. The salt is random per boot and
// never persisted, so the same device gets a different pseudonym next
// session: enough to correlate within one sweep, without building a permanent
// record of every device belonging to everyone in the building.
//
// Pseudonymisation is not anonymity - names, SSIDs and manufacturer payloads
// can still be distinctive. Treat exported logs as sensitive.
//
// Frame metadata only. Never payloads.
// ===========================================================================

bool  s_sd_ok = false;
char  s_fname[40];
File  s_file;
uint8_t s_salt[16];

// Strip anything that would break a CSV row. Applied to every free-text
// field, since vendor and model strings come straight off the air.
void csv_safe(char *dst, size_t cap, const char *src)
{
    size_t k = 0;
    for (size_t i = 0; src[i] && k < cap - 1; i++) {
        char c = src[i];
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = '_';
        dst[k++] = c;
    }
    dst[k] = 0;
}

void log_pseudonym(const uint8_t mac[6], char *out, size_t cap)
{
    uint8_t buf[22], h[32];
    memcpy(buf, s_salt, 16);
    memcpy(buf + 16, mac, 6);
    mbedtls_sha256(buf, sizeof(buf), h, 0);
    snprintf(out, cap, "%02x%02x%02x%02x%02x%02x%02x%02x",
             h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]);
}

bool log_init()
{
    esp_fill_random(s_salt, sizeof(s_salt));
    spi_take();
    if (!SD.begin(PIN_SD_CS, SPI, SPI_HZ_SD)) { spi_give(); s_sd_ok = false; return false; }
    if (!SD.exists(LOG_DIR)) SD.mkdir(LOG_DIR);
    // No RTC on this board, so number the sessions rather than timestamp them.
    for (int n = 1; n < 1000; n++) {
        snprintf(s_fname, sizeof(s_fname), LOG_DIR "/vulp_%03d.csv", n);
        if (!SD.exists(s_fname)) break;
    }
    s_file = SD.open(s_fname, FILE_WRITE);
    if (!s_file) { spi_give(); s_sd_ok = false; return false; }
    s_file.println("# Vulpecula RF sweep log. Session-salted MAC hashes;");
    s_file.println("# the salt is random per boot and never persisted.");
    s_file.println("# Frame metadata only - no payloads.");
    s_file.println("event,ms,pseudonym,band,ch,name,tier,peak,median,dist_m,"
                   "score,conf,risk,why,dtype,dtier,vendor,model,"
                   "pkts,rate,up_kB,down_kB,evidence,phase");
    s_file.flush();
    spi_give();
    s_sd_ok = true;
    return true;
}

// Runs on the PUMP task while the UI task is drawing, so it must hold the SPI
// mutex for the whole write - the SD card shares the bus with the display.
void log_snapshot(uint32_t now, const char *event)
{
    if (!s_sd_ok) return;
    spi_take();
    static track_t *list[MAX_TRACKS];
    uint16_t n = tracks_sorted(list, MAX_TRACKS);
    bool baseline = (strcmp(event, "BASELINE") == 0);

    for (uint16_t i = 0; i < n; i++) {
        track_t *t = list[i];
        // Keep the main log readable: only tracks that crossed the gate or
        // scored. Ambient-only entries stay in the table for the diff and get
        // written on the BASELINE event.
        if (!(t->tier_best >= TIER_NEAR || t->score >= SCORE_POSSIBLE || baseline))
            continue;

        char pseudo[20];
        log_pseudonym(t->mac, pseudo, sizeof(pseudo));
        int8_t med = ring_median(&t->ring, now, MEDIAN_WINDOW_MS);

        char safe[NAME_LEN];
        csv_safe(safe, sizeof(safe), t->name);

        char vsafe[24], msafe[20];
        csv_safe(vsafe, sizeof(vsafe), t->vendor);
        csv_safe(msafe, sizeof(msafe), t->model);

        s_file.printf("%s,%lu,%s,%s,%u,%s,%s,%d,%d,%.2f,%u,%s,"
                      "%s,%s,%s,%s,%s,%s,%lu,%lu,%lu,%lu,0x%08lx,%d\n",
            event, (unsigned long)now, pseudo, band_name(t->band),
            (unsigned)t->channel, safe, tier_name(t->tier_best),
            (int)t->peak_session, (int)(med == INT8_MIN ? 0 : med),
            (double)t->dist_m, (unsigned)t->score,
            confidence_name(classify_confidence(t)),
            risk_name(t->risk), t->risk_why ? t->risk_why : "",
            dtype_name(t->dtype), dtier_name(t->dtype_tier), vsafe, msafe,
            (unsigned long)t->pkts, (unsigned long)t->pkt_rate,
            (unsigned long)(t->bytes_up/1024), (unsigned long)(t->bytes_down/1024),
            (unsigned long)t->evidence, (int)g_phase);
    }
    s_file.flush();
    spi_give();
}

// Delete every sweep log on the card, and start a fresh session file.
//
// Deliberately NOT a secure erase - this unlinks files on an SD card, and
// the blocks remain until the controller reuses them. It defeats someone
// reading the card normally, not someone imaging the flash. If that is in
// your threat model, encrypt the log instead, or destroy the card.
//
// Returns how many files were removed.
uint16_t log_wipe(void)
{
    if (!s_sd_ok) return 0;
    uint16_t removed = 0;

    spi_take();
    if (s_file) s_file.close();

    File dir = SD.open(LOG_DIR);
    if (dir) {
        // Collect names first: removing entries while walking the directory
        // is not reliable on this library.
        char names[64][40];
        uint8_t n = 0;
        File e;
        while (n < 64 && (e = dir.openNextFile())) {
            const char *nm = e.name();
            size_t ln = strlen(nm);
            // Only our own sweep logs. oui.csv, oui.bin, rules.csv and
            // names.csv are configuration, not session data, and wiping the
            // operator's imported vendor database would be a nasty surprise.
            if (ln > 4 && !strcasecmp(nm + ln - 4, ".csv") &&
                strncasecmp(nm, "vulp_", 5) == 0) {
                snprintf(names[n], sizeof(names[n]), LOG_DIR "/%s", nm);
                n++;
            }
            e.close();
        }
        dir.close();
        for (uint8_t i = 0; i < n; i++)
            if (SD.remove(names[i])) removed++;
    }
    spi_give();

    // Re-open a fresh session with a NEW salt, so observations from before
    // the wipe cannot be correlated with anything logged after it.
    s_sd_ok = false;
    log_init();

    Serial.printf("[log] wiped %u sweep file(s), new session %s\n",
                  (unsigned)removed, s_sd_ok ? s_fname : "(none)");
    return removed;
}
