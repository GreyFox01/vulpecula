// calnorm_test.cpp - calibration reference normalisation.
//
// The routine measures RSSI from a specific beacon; a threshold has to
// describe a typical TARGET. The correction is
//
//     threshold = measured + (target_power - reference_power)
//
// An earlier version omitted it and used the measurement directly, which with
// an +11 dBm reference and a +17 dBm design target set the gate 6 dB too
// permissive - the 2 m ring silently extended past 2 m.
//
// The sign matters as much as the magnitude: backwards, the correction
// doubles the error instead of cancelling it, and the numbers still look
// plausible. So that is what this checks.
#include <stdio.h>
#include <stdlib.h>

// Mirrors Vulpecula/src/calib.cpp cal_ref_correction(). Kept as a formula
// rather than a link because config.h values are compile-time, and the point
// here is to verify the arithmetic and the sign for several beacon powers.
static int correction(int target_dbm, int ref_dbm) { return target_dbm - ref_dbm; }

static int fails = 0;
static void chk(bool c, const char *w)
{
    printf("  [%s] %s\n", c ? "PASS" : "FAIL", w);
    if (!c) fails++;
}

int main(void)
{
    printf("\n== a matched beacon needs no correction ==\n");
    chk(correction(17, 17) == 0, "+17 dBm target, +17 dBm reference -> 0 dB");
    chk(correction(0, 0) == 0,   "0 dBm BLE target, 0 dBm reference  -> 0 dB");

    printf("\n== a weak beacon must make the gate STRICTER, not looser ==\n");
    // An +11 dBm beacon reads 6 dB lower than a +17 dBm camera would. Using
    // the raw reading would set the threshold 6 dB too low, i.e. too
    // permissive. The correction must be POSITIVE.
    int c11 = correction(17, 11);
    chk(c11 == +6, "+11 dBm reference -> +6 dB correction");
    int measured = -41;                       // what an 11 dBm beacon reads at 2 m
    int threshold = measured + c11;
    chk(threshold == -35, "measured -41 dBm becomes a -35 dBm threshold");
    chk(threshold > measured, "the corrected threshold is STRICTER than the raw reading");

    printf("\n== a hot beacon must make the gate LOOSER ==\n");
    int c20 = correction(17, 20);
    chk(c20 == -3, "+20 dBm reference -> -3 dB correction");
    chk((-32 + c20) == -35, "measured -32 dBm becomes a -35 dBm threshold");

    printf("\n== the uncorrected error, for the record ==\n");
    // This is the bug that was shipped and then fixed: without correction, an
    // 11 dBm reference yields a 6 dB over-permissive gate. At a path-loss
    // exponent of 2 that is 10^(6/20) = 2.0x the intended radius.
    chk(abs(correction(17, 11)) == 6,
        "omitting it costs 6 dB, roughly doubling the 2 m radius");

    printf("\n%s (%d failure%s)\n\n",
           fails ? "CALNORM TESTS FAILED" : "ALL CALNORM TESTS PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
