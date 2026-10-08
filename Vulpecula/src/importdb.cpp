// importdb.cpp - SD import of vendor and signature data into PSRAM.
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
// IMPORTED VENDOR AND SIGNATURE DATABASE
//
// Characterisation data lives on the SD card and is imported into PSRAM, so
// adding vendors or brand rules needs a text editor and a card reader, not a
// reflash.
//
// With 8 MB of PSRAM the whole IEEE registry fits in RAM: ~35,000 assignments
// at 32 bytes each is about 1.1 MB. So this is not a compromise against the
// compiled table, it replaces it outright. The compiled table stays only as a
// fallback for a board with no card in it.
//
// THREE FILES, AND THE SPLIT BETWEEN THEM MATTERS
//
//   /vulpecula/oui.csv    FACTS. The IEEE registry as downloaded. Maps a
//                         prefix to an organisation name. No judgement in it,
//                         and nothing in it says what a device DOES.
//
//   /vulpecula/rules.csv  JUDGEMENT. Maps an organisation-name keyword to a
//                         device type and a confidence weight, applied once
//                         at import. This is where "Hikvision makes cameras"
//                         lives, and it is the file you will actually tune.
//
//   /vulpecula/names.csv  Patterns matched at runtime against SSIDs and BLE
//                         advertised names, for devices whose prefix says
//                         nothing useful.
//
// Keeping facts and judgement in separate files is deliberate. The registry
// is authoritative and should be replaced wholesale when it updates; the
// rules are opinions that must survive that replacement. Mixing them would
// mean re-deciding every brand every time the registry changes.
//
// A parsed registry is cached as /vulpecula/oui.bin on first import, because
// parsing 35,000 CSV lines off SD takes long enough to be irritating and the
// binary form loads in a couple of seconds.
// ===========================================================================

// Import progress, polled by the import page.
volatile uint32_t g_imp_lines = 0, g_imp_kept = 0;
const char *g_imp_stage = "idle";
bool g_imp_running = false;

const char *F_OUI_CSV   = LOG_DIR "/oui.csv";
const char *F_OUI_BIN   = LOG_DIR "/oui.bin";
const char *F_RULES_CSV = LOG_DIR "/rules.csv";
const char *F_NAMES_CSV = LOG_DIR "/names.csv";

// --- the judgement layer ---------------------------------------------------

void rules_load()
{
    g_rules_n = 0;
    if (!s_sd_ok) return;
    spi_take();
    File f = SD.open(F_RULES_CSV, FILE_READ);
    if (f) {
        char line[160];
        while (f.available() && g_rules_n < RULES_MAX) {
            int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            if (n <= 0) continue;
            line[n] = 0;
            if (line[0] == '#' || line[0] == 0) continue;
            char *fld[4];
            int nf = csv_split(line, fld, 4);
            if (nf < 2) continue;
            if (!strcasecmp(fld[0], "keyword")) continue;      // header row
            vrule_t *r = &g_rules[g_rules_n];
            strncpy(r->kw, fld[0], sizeof(r->kw) - 1);
            r->kw[sizeof(r->kw) - 1] = 0;
            str_lower(r->kw);
            r->dtype  = (uint8_t)dtype_from_text(nf > 1 ? fld[1] : NULL);
            r->weight = weight_from_text(nf > 2 ? fld[2] : NULL);
            if (r->kw[0]) g_rules_n++;
        }
        f.close();
    }
    spi_give();
}

// --- runtime name patterns -------------------------------------------------
void namesig_load()
{
    g_namesig_n = 0;
    if (!s_sd_ok) return;
    if (!g_namesig)
        g_namesig = (namesig_t *)heap_caps_calloc(NAMESIG_MAX, sizeof(namesig_t),
                                                  MALLOC_CAP_SPIRAM);
    if (!g_namesig) return;

    spi_take();
    File f = SD.open(F_NAMES_CSV, FILE_READ);
    if (f) {
        char line[160];
        while (f.available() && g_namesig_n < NAMESIG_MAX) {
            int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
            if (n <= 0) continue;
            line[n] = 0;
            if (line[0] == '#' || line[0] == 0) continue;
            char *fld[4];
            int nf = csv_split(line, fld, 4);
            if (nf < 1 || !strcasecmp(fld[0], "pattern")) continue;
            namesig_t *s = &g_namesig[g_namesig_n];
            strncpy(s->pat, fld[0], NAMESIG_PAT_LEN - 1);
            s->pat[NAMESIG_PAT_LEN - 1] = 0;
            str_lower(s->pat);
            const char *sc = (nf > 1) ? fld[1] : "any";
            s->scope  = !strcasecmp(sc, "ssid") ? NS_SSID
                      : !strcasecmp(sc, "ble")  ? NS_BLE : NS_ANY;
            s->dtype  = (uint8_t)dtype_from_text(nf > 2 ? fld[2] : NULL);
            s->weight = weight_from_text(nf > 3 ? fld[3] : NULL);
            if (s->pat[0]) g_namesig_n++;
        }
        f.close();
    }
    spi_give();
}

bool oui_db_alloc()
{
    if (g_oui_db) return true;
    g_oui_db = (oui_rec_t *)heap_caps_calloc(OUI_DB_MAX, sizeof(oui_rec_t),
                                             MALLOC_CAP_SPIRAM);
    if (!g_oui_db) Serial.println("[import] PSRAM alloc for OUI db failed");
    return g_oui_db != NULL;
}

// Fast path: a previously parsed binary cache.
bool oui_load_bin()
{
    if (!s_sd_ok || !oui_db_alloc()) return false;
    g_imp_stage = "loading oui.bin";

    spi_take();
    File f = SD.open(F_OUI_BIN, FILE_READ);
    bool ok = false;
    if (f) {
        oui_bin_hdr_t h;
        if (f.read((uint8_t *)&h, sizeof(h)) == (int)sizeof(h) &&
            h.magic == OUI_BIN_MAGIC && h.version == OUI_BIN_VER &&
            h.count > 0 && h.count <= OUI_DB_MAX) {
            // Read in chunks; one record at a time over SPI is painfully slow.
            uint32_t want = h.count * sizeof(oui_rec_t), got = 0;
            uint8_t *dst = (uint8_t *)g_oui_db;
            while (got < want) {
                int n = f.read(dst + got, (want - got) > 4096 ? 4096 : (want - got));
                if (n <= 0) break;
                got += (uint32_t)n;
                g_imp_kept = got / sizeof(oui_rec_t);
            }
            if (got == want) { g_oui_n = h.count; ok = true; }
        }
        f.close();
    }
    spi_give();
    return ok;
}

void oui_write_bin()
{
    if (!s_sd_ok || !g_oui_n) return;
    g_imp_stage = "writing cache";
    spi_take();
    if (SD.exists(F_OUI_BIN)) SD.remove(F_OUI_BIN);
    File f = SD.open(F_OUI_BIN, FILE_WRITE);
    if (f) {
        oui_bin_hdr_t h = { OUI_BIN_MAGIC, OUI_BIN_VER, g_oui_n, 0 };
        f.write((const uint8_t *)&h, sizeof(h));
        uint32_t want = g_oui_n * sizeof(oui_rec_t), done = 0;
        const uint8_t *src = (const uint8_t *)g_oui_db;
        while (done < want) {
            uint32_t n = (want - done) > 4096 ? 4096 : (want - done);
            if (f.write(src + done, n) != n) break;
            done += n;
        }
        f.close();
    }
    spi_give();
}

// Slow path: parse the IEEE registry. Accepts either the official 4-column
// export or a plain "prefix,vendor" two-column file.
bool oui_parse_csv()
{
    if (!s_sd_ok || !oui_db_alloc()) return false;
    g_imp_stage = "parsing oui.csv";
    g_imp_lines = g_imp_kept = 0;
    g_oui_n = 0;

    spi_take();
    File f = SD.open(F_OUI_CSV, FILE_READ);
    if (!f) { spi_give(); return false; }

    char line[256];
    while (f.available() && g_oui_n < OUI_DB_MAX) {
        int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
        if (n <= 0) continue;
        line[n] = 0;
        g_imp_lines++;

        char *fld[6];
        int nf = csv_split(line, fld, 6);
        if (nf < 2) continue;

        // The official export is Registry,Assignment,Organization Name,...
        // so the prefix is field 1. A hand-made file has it in field 0.
        const char *pfx = NULL, *org = NULL;
        uint8_t oui[3];
        if (nf >= 3 && hex3(fld[1], oui)) { pfx = fld[1]; org = fld[2]; }
        else if (hex3(fld[0], oui))       { pfx = fld[0]; org = fld[1]; }
        else continue;                    // header row or junk
        (void)pfx;
        if (!org || !org[0]) continue;

        oui_rec_t *r = &g_oui_db[g_oui_n];
        memcpy(r->oui, oui, 3);
        strncpy(r->vendor, org, VENDOR_NAME_LEN - 1);
        r->vendor[VENDOR_NAME_LEN - 1] = 0;
        rules_apply(r->vendor, &r->dtype, &r->weight);
        r->pad = 0;
        g_oui_n++;
        g_imp_kept = g_oui_n;

        // Yield so the UI task still draws progress and the watchdog is fed.
        if ((g_oui_n & 0x1FF) == 0) { spi_give(); vTaskDelay(1); spi_take(); }
    }
    f.close();
    spi_give();

    g_imp_stage = "sorting";
    qsort(g_oui_db, g_oui_n, sizeof(oui_rec_t), cmp_oui);
    return g_oui_n > 0;
}

// --- the user-facing import ------------------------------------------------
// Runs on the UI task, which is why it yields: a 35k-line parse takes long
// enough that the display must keep updating or it looks hung.
bool import_run(bool force_reparse)
{
    if (g_imp_running) return false;
    g_imp_running = true;
    g_imp_lines = g_imp_kept = 0;

    g_imp_stage = "reading rules.csv";
    rules_load();
    g_imp_stage = "reading names.csv";
    namesig_load();

    bool ok = false;
    if (!force_reparse && oui_load_bin()) {
        ok = true;
        g_imp_stage = "loaded from cache";
    } else if (oui_parse_csv()) {
        oui_write_bin();
        ok = true;
        g_imp_stage = "imported and cached";
    } else {
        g_imp_stage = g_oui_n ? "partial" : "no oui.csv found";
    }

    Serial.printf("[import] rules %lu  names %lu  oui %lu  (%s)\n",
                  (unsigned long)g_rules_n, (unsigned long)g_namesig_n,
                  (unsigned long)g_oui_n, g_imp_stage);
    g_imp_running = false;
    return ok;
}

// Write starter files so the card is self-documenting rather than requiring
// the operator to remember three formats.
void import_write_templates()
{
    if (!s_sd_ok) return;
    spi_take();
    if (!SD.exists(F_RULES_CSV)) {
        File f = SD.open(F_RULES_CSV, FILE_WRITE);
        if (f) {
            f.println("# rules.csv - JUDGEMENT layer, applied to oui.csv at import.");
            f.println("# keyword matched case-insensitively inside the vendor name.");
            f.println("# First match wins, so put specific before generic.");
            f.println("# type: camera mic nvr av speaker phone computer network");
            f.println("#       display wearable printer tag unknown");
            f.println("# weight: strong = makes little else. weak = mixed portfolio,");
            f.println("#         so a hit names the brand but not the device.");
            f.println("keyword,type,weight");
            f.println("hikvision,camera,strong");
            f.println("dahua,camera,strong");
            f.println("uniview,camera,strong");
            f.println("axis communication,camera,strong");
            f.println("vivotek,camera,strong");
            f.println("mobotix,camera,strong");
            f.println("hanwha,camera,strong");
            f.println("avigilon,camera,strong");
            f.println("foscam,camera,strong");
            f.println("reolink,camera,strong");
            f.println("amcrest,camera,strong");
            f.println("ezviz,camera,strong");
            f.println("vstarcam,camera,strong");
            f.println("gopro,camera,strong");
            f.println("arashi vision,camera,strong");
            f.println("rode,mic,strong");
            f.println("hollyland,mic,strong");
            f.println("saramonic,mic,strong");
            f.println("shure,mic,strong");
            f.println("sennheiser,mic,strong");
            f.println("# mixed portfolios - brand only, no device type");
            f.println("dji,unknown,weak");
            f.println("ring,unknown,weak");
            f.println("google,unknown,weak");
            f.println("amazon,unknown,weak");
            f.println("xiaomi,unknown,weak");
            f.println("tp-link,unknown,weak");
            f.println("tuya,unknown,weak");
            f.println("# SoC and module vendors inside no-name cameras.");
            f.println("# Shared with masses of unrelated hardware: never strong.");
            f.println("realtek,unknown,weak");
            f.println("ingenic,unknown,weak");
            f.println("anyka,unknown,weak");
            f.println("espressif,unknown,weak");
            f.close();
        }
    }
    if (!SD.exists(F_NAMES_CSV)) {
        File f = SD.open(F_NAMES_CSV, FILE_WRITE);
        if (f) {
            f.println("# names.csv - matched at runtime against SSIDs and BLE names.");
            f.println("# For devices whose MAC prefix says nothing useful, which is");
            f.println("# most no-name cameras.");
            f.println("# scope: ssid | ble | any");
            f.println("pattern,scope,type,weight");
            f.println("ipcam,ssid,camera,strong");
            f.println("ipc-,ssid,camera,strong");
            f.println("v380,ssid,camera,strong");
            f.println("yicam,ssid,camera,strong");
            f.println("goolink,ssid,camera,strong");
            f.println("lookcam,ssid,camera,strong");
            f.println("littlestar,ssid,camera,strong");
            f.println("anycam,ssid,camera,strong");
            f.println("netcam,ssid,camera,strong");
            f.println("wificam,ssid,camera,strong");
            f.println("smartcam,ssid,camera,strong");
            f.println("cloudcam,ssid,camera,strong");
            f.println("camera,ssid,camera,weak");
            f.println("dvr,ssid,nvr,weak");
            f.println("gopro,ble,camera,strong");
            f.println("insta360,ble,camera,strong");
            f.println("sjcam,ble,camera,strong");
            f.println("bodycam,any,camera,strong");
            f.println("spycam,any,camera,strong");
            f.println("dji mic,ble,mic,strong");
            f.println("wireless go,ble,mic,strong");
            f.println("hollyland,ble,mic,strong");
            f.println("lark,ble,mic,strong");
            f.println("wireless mic,any,mic,strong");
            f.println("boya,ble,mic,strong");
            f.println("comica,ble,mic,strong");
            f.println("saramonic,ble,mic,strong");
            f.close();
        }
    }
    if (!SD.exists(F_OUI_CSV)) {
        File f = SD.open(F_OUI_CSV, FILE_WRITE);
        if (f) {
            f.println("# oui.csv - FACTS layer. Replace this with the IEEE registry:");
            f.println("#   https://standards-oui.ieee.org/oui/oui.csv");
            f.println("# Either the official 4-column export or plain prefix,vendor");
            f.println("# works. Nothing here says what a device does - that is");
            f.println("# rules.csv, so replacing this file wholesale when the");
            f.println("# registry updates does not lose your judgement calls.");
            f.println("Registry,Assignment,Organization Name,Organization Address");
            f.println("MA-L,4419B6,Hangzhou Hikvision Digital Technology,Hangzhou");
            f.println("MA-L,4C11BF,Zhejiang Dahua Technology,Hangzhou");
            f.println("MA-L,ACCC8E,Axis Communications AB,Lund");
            f.close();
        }
    }
    spi_give();
}
