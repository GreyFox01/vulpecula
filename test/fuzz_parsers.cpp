// fuzz_parsers.cpp - fuzz the parsers that eat attacker-controlled data.
//
// WHY THIS EXISTS
//
// Three functions in this firmware process bytes that an attacker fully
// controls, and nothing else does:
//
//   parse_mgmt_ies()  802.11 beacon and probe-response information elements
//   parse_wps()       WPS attributes nested inside a vendor-specific IE
//   parse_ble_adv()   BLE advertisement AD structures
//
// Every length field in those is supplied by the transmitter. A hostile
// beacon is the cheapest possible attack on a device whose whole job is to
// sit and listen to beacons, so these are the functions worth fuzzing.
//
// They are pure by construction - src/parsers.cpp depends on nothing from
// Arduino - which is what makes this file possible at all. Build it with
// ASan and UBSan, run it, and any out-of-bounds read or signed overflow stops
// the build.
//
// Two modes:
//
//   Default      a self-contained random fuzzer with a fixed seed, so it runs
//                in CI on any machine with g++ and needs no extra toolchain.
//                Plus a hand-written corpus of the shapes that actually break
//                length-prefixed parsers: zero lengths, lengths past the end,
//                nesting that claims more than it has, and truncation at
//                every single byte offset.
//
//   -DUSE_LIBFUZZER   builds LLVMFuzzerTestOneInput instead, for a real
//                     coverage-guided run with clang. Worth doing before a
//                     release; the built-in mode is for every commit.
//
// Build (CI):
//   g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined \
//       -I ../Vulpecula/src fuzz_parsers.cpp \
//       ../Vulpecula/src/parsers.cpp ../Vulpecula/src/proximity.cpp \
//       ../Vulpecula/src/vendor.cpp -o /tmp/fuzz && /tmp/fuzz
//
// Build (coverage-guided):
//   clang++ -std=gnu++17 -O1 -g -fsanitize=fuzzer,address,undefined \
//       -DUSE_LIBFUZZER -I ../Vulpecula/src fuzz_parsers.cpp \
//       ../Vulpecula/src/parsers.cpp ../Vulpecula/src/proximity.cpp \
//       ../Vulpecula/src/vendor.cpp -o /tmp/fuzz && /tmp/fuzz -max_total_time=120

#include "pure.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>

// A fresh track for every case, so one input cannot influence the next.
static void feed(const uint8_t *d, size_t n)
{
    if (n == 0) return;

    // 802.11 management frame path. parse_mgmt_ies starts reading tagged
    // parameters at offset 36, so anything shorter must be rejected rather
    // than walked.
    {
        track_t t;
        memset(&t, 0, sizeof(t));
        t.band = BAND_24;
        t.ref.src = REF_NONE;
        size_t cap = n > IE_BUF_LEN ? IE_BUF_LEN : n;
        parse_mgmt_ies(&t, d, (uint16_t)cap);
    }

    // The WPS attribute walk, reached directly as well as through the IE
    // walk, since a vendor IE may be malformed in ways the outer walk allows.
    {
        track_t t;
        memset(&t, 0, sizeof(t));
        t.band = BAND_5;
        t.ref.src = REF_NONE;
        size_t cap = n > 512 ? 512 : n;
        parse_wps(&t, d, (uint16_t)cap);
    }

    // BLE advertisement path.
    {
        track_t t;
        memset(&t, 0, sizeof(t));
        t.band = BAND_BLE;
        t.ref.src = REF_NONE;
        size_t cap = n > 62 ? 62 : n;
        parse_ble_adv(&t, d, (uint8_t)cap);
    }

    // The CSV splitter reads the import files. Those are less hostile than the
    // air, but a corrupt card should not crash a detector mid-sweep.
    {
        char line[300];
        size_t cap = n > sizeof(line) - 1 ? sizeof(line) - 1 : n;
        memcpy(line, d, cap);
        line[cap] = 0;
        char *fld[6];
        csv_split(line, fld, 6);
    }
}

#ifdef USE_LIBFUZZER
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
    feed(d, n);
    return 0;
}
#else

// ---------------------------------------------------------------------------
// Hand-written corpus. These are the shapes that break length-prefixed
// parsers, written out rather than left to chance, because random bytes find
// them only rarely.
// ---------------------------------------------------------------------------
static void corpus()
{
    const uint8_t empty[1] = { 0 };
    feed(empty, 0);
    feed(empty, 1);

    // An IE claiming a length that runs past the buffer.
    {
        uint8_t f[40];
        memset(f, 0, sizeof(f));
        f[36] = 0x00;        // SSID element
        f[37] = 0xFF;        // ...of 255 bytes, in a 40-byte frame
        feed(f, sizeof(f));
    }
    // A vendor IE that claims WPS but has no body.
    {
        uint8_t f[44];
        memset(f, 0, sizeof(f));
        f[36] = 0xDD; f[37] = 0x04;
        f[38] = 0x00; f[39] = 0x50; f[40] = 0xF2; f[41] = 0x04;
        feed(f, sizeof(f));
    }
    // A WPS attribute whose length overruns, inside an IE whose length does not.
    {
        uint8_t f[64];
        memset(f, 0, sizeof(f));
        f[36] = 0xDD; f[37] = 20;
        f[38] = 0x00; f[39] = 0x50; f[40] = 0xF2; f[41] = 0x04;
        f[42] = 0x10; f[43] = 0x21;      // Manufacturer
        f[44] = 0xFF; f[45] = 0xFF;      // length 65535
        feed(f, sizeof(f));
    }
    // Primary Device Type truncated one byte short of its 8.
    {
        uint8_t f[64];
        memset(f, 0, sizeof(f));
        f[36] = 0xDD; f[37] = 15;
        f[38] = 0x00; f[39] = 0x50; f[40] = 0xF2; f[41] = 0x04;
        f[42] = 0x10; f[43] = 0x54;
        f[44] = 0x00; f[45] = 0x07;      // claims 7 bytes, needs 8
        feed(f, sizeof(f));
    }
    // A zero-length AD structure, which must terminate the walk rather than
    // loop on it.
    {
        uint8_t a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        feed(a, sizeof(a));
    }
    // An iBeacon header one byte short of the measured-power field.
    {
        uint8_t a[27];
        memset(a, 0, sizeof(a));
        a[0] = 25; a[1] = 0xFF; a[2] = 0x4C; a[3] = 0x00;
        a[4] = 0x02; a[5] = 0x15;
        feed(a, 26);
        feed(a, 6);
    }
    // Eddystone service data with no ranging byte.
    {
        uint8_t a[8] = { 4, 0x16, 0xAA, 0xFE, 0x00, 0, 0, 0 };
        feed(a, 5);
        feed(a, sizeof(a));
    }
    // An AD length that points exactly at the final byte, and one past it.
    for (uint8_t len = 0; len < 40; len++) {
        uint8_t a[40];
        memset(a, 0x41, sizeof(a));
        a[0] = len; a[1] = 0x09;        // complete local name
        feed(a, 32);
    }
    // Unterminated quoted CSV field, and a line that is only separators.
    {
        const char *q = "\"unterminated,field,with,commas";
        feed((const uint8_t *)q, strlen(q));
        const char *c = ",,,,,,,,,,";
        feed((const uint8_t *)c, strlen(c));
    }
}

// Truncate a known-good frame at every offset. Off-by-one errors in a
// length-prefixed walk show up here far more reliably than at random.
static void truncations()
{
    uint8_t f[80];
    memset(f, 0, sizeof(f));
    f[36] = 0x00; f[37] = 6;
    memcpy(&f[38], "IPCAM1", 6);
    f[44] = 0xDD; f[45] = 22;
    f[46] = 0x00; f[47] = 0x50; f[48] = 0xF2; f[49] = 0x04;
    f[50] = 0x10; f[51] = 0x54; f[52] = 0x00; f[53] = 0x08;
    f[54] = 0x00; f[55] = 0x04;                      // category 4: camera
    f[56] = 0x00; f[57] = 0x50; f[58] = 0xF2; f[59] = 0x04;
    f[60] = 0x00; f[61] = 0x04;                      // security camera
    for (size_t n = 0; n <= sizeof(f); n++) feed(f, n);
}

// xorshift32 so the run is identical on every machine; a fuzzer that finds a
// crash only on the CI box is not much use.
static uint32_t rng_state = 0x12345678u;
static uint32_t rng()
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(int argc, char **argv)
{
    long iters = (argc > 1) ? atol(argv[1]) : 200000;

    printf("corpus: the shapes that break length-prefixed parsers\n");
    corpus();
    printf("truncation: a valid frame cut at every one of 81 offsets\n");
    truncations();

    printf("random: %ld iterations, fixed seed, lengths 0-400\n", iters);
    std::vector<uint8_t> buf(400);
    for (long i = 0; i < iters; i++) {
        size_t n = rng() % buf.size();
        for (size_t k = 0; k < n; k++) buf[k] = (uint8_t)(rng() & 0xFF);
        feed(buf.data(), n);

        // Half the time, keep a plausible frame header so the walk gets past
        // its first guard. Pure noise rarely reaches the interesting code.
        if (i & 1) {
            if (n > 40) {
                buf[36] = (uint8_t)(rng() & 1 ? 0x00 : 0xDD);
                buf[37] = (uint8_t)(rng() & 0x3F);
            }
            feed(buf.data(), n);
        }
    }

    printf("\nFUZZ OK - no sanitizer findings\n\n");
    return 0;
}
#endif
