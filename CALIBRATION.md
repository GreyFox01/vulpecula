# Calibration

The thresholds shipped in `config.h` are path-loss maths plus an assumed
implementation margin. They are a starting point, not a measurement of your
board. Antenna choice, whether the 0-ohm RF selector actually routes to your
U.FL socket, and board-to-board variation all move them by several dB.

Two experiments, both about thirty minutes, and both worth doing before you
trust a single reading.

---

## Experiment 1: does DFS work here?

Answered automatically at boot. Flash, watch the serial console:

```
probing 5 GHz channels...
5 GHz usable: 25 channels, DFS yes
```

Also shown on the SETUP screen. Three outcomes:

- **25 channels, DFS yes** — full coverage. Good.
- **9 channels, DFS no** — you have UNII-1 (36–48) and UNII-3 (149–165) but
  lost 52–144. A real gap; plenty of consumer cameras sit there. Consider
  setting a different country in the regulatory config if lawful where you are.
- **0 channels** — no 5 GHz at all. Something is wrong with the band-mode call
  or the core version. Fix this before going further, because 5 GHz coverage is
  the main reason to build on a C5.

---

## Experiment 2: threshold calibration

### You need

- A reference transmitter whose position you control. Use a second ESP32
  running `tools/reference_beacon/` (below), *not* your phone — you need a
  known, constant transmit power and a name you cannot mistake for a real
  find.
- A tape measure.
- A room you can leave alone for twenty minutes. Empty-ish, no people moving.

### Procedure

Repeat per band: 2.4 GHz, 5 GHz, BLE.

1. Put the CYD on a non-metallic surface at roughly chest height. Do not hold
   it — your hand costs several dB and it will not be there consistently.
2. Place the reference at a tape-measured **2.00 m**, same height, clear line
   of sight.
3. On the CYD: SETUP → tap the right half to start calibration. The prompt
   reads `Reference at 2.0 m. Hold still.`
4. Wait for the sample count to pass 40. Leave the room if you can — a body
   between the two is worth 3–6 dB.
5. Tap right half again. Prompt: `Move reference to 0.3 m.`
6. Move the reference to **0.30 m**. Wait for 40+ samples.
7. Tap right half. You should see `Done. Commit to save thresholds.`
8. Tap once more to commit. Values persist to NVS.

The routine takes the **80th percentile**, not the mean, because the gate fires
on peak. High enough to represent peak behaviour, low enough that one multipath
spike does not set the threshold.

### Acceptance criteria

Calibration fails if the two stages are less than 8 dB apart. That almost
always means one of:

- the reference never actually moved
- you measured 2 m but the reference was behind something
- the antenna is not connected, or the 0-ohm selector was never moved, so
  you are reading the PCB antenna at a fraction of the expected level

A healthy 2.4 GHz result looks roughly like NEAR ≈ −30 to −40 and
CONTACT ≈ −10 to −20, with about 16 dB between them (the free-space delta
between 2.0 m and 0.3 m is 16.5 dB). If your measured delta is much *smaller*
than 16 dB, the room is reflective enough that near-field readings are
compressed — note it, because your distance estimates will be optimistic.

### Sanity check afterwards

Take the reference to the far side of an interior wall at 2 m and confirm it
reads ambient. If it still shows NEAR, the wall is thinner than the design
assumed and you should lower `near_thresh` for that band by the difference.
This is the single most important check, because rejecting the next room is the
whole point.

---

## Reference beacon sketch

Shipped as `Vulpecula/reference_beacon/reference_beacon.ino`. Flash to
any spare ESP32 - it does not need to be a C5. It advertises with a name
nobody could mistake for a genuine find, at a fixed transmit power, and
includes an explicit TX Power Level AD field so you can exercise the
reference-based BLE path rather than only the raw-RSSI fallback.


Note the name is deliberately alarming. A calibration transmitter that could
be confused with a real detection during a later sweep is a hazard, and
`DO-NOT-TRUST` in the name means it can never quietly end up in a report.

---

## The reference beacon

Flash `reference_beacon/reference_beacon.ino` to a **second NM-CYD-C5**. It is
the only common board that can transmit on 5 GHz, and Vulpecula needs a 5 GHz
threshold.

Board **ESP32C5 Dev Module**, **USB CDC On Boot: Enabled**, 115200 baud. The
screen stays dark on purpose — the beacon is headless so it remains small
enough to audit. The RGB LED shows the band: **blue** 2.4 GHz, **magenta**
5 GHz, **red** the radio refused the requested power.

On boot it prints:

```
=== Vulpecula calibration reference ===
  SSID      : VULP-CAL-W17-DO-NOT-TRUST
  band      : 2.4 GHz
  WiFi power: 17.00 dBm asked, 17.00 dBm accepted
  BLE power : 0 dBm asked, 0 dBm accepted
```

**Check asked against accepted.** If they differ by more than 1 dB the beacon
says so, tells you what to set `CAL_REF_WIFI_DBM` to, and turns the LED red.
Calibrating through a mismatch produces plausible-looking numbers that are
wrong by exactly that difference.

Switch bands over serial — `2`, `5`, `s` for status — so you do not reflash
between the two Wi-Fi calibrations.

## Why the measured RSSI is not the threshold

The routine measures RSSI from *your beacon* at 2.00 m. A threshold has to
describe what a *typical target* would produce at the same distance. So:

```
threshold = measured + (target_power - reference_power)
```

An earlier version of this firmware skipped that correction and used the
measurement directly. With an +11 dBm reference and a gate meant to catch a
+17 dBm camera, that set the threshold **6 dB too permissive** — the 2 metre
ring silently extended well past 2 metres, in the one component whose whole
job is to be right about distance.

The beacon therefore defaults to the design-assumption powers, +17 dBm Wi-Fi
and 0 dBm BLE, so the correction is zero and there is nothing to get wrong.
The RSSI CAL page shows the correction being applied:

```
Ready: 2.4G band, ref correction +0 dB
```

If you change the beacon's power, change `CAL_REF_WIFI_DBM` and
`CAL_REF_BLE_DBM` in `Vulpecula/src/config.h` to match.

## Pick the band explicitly

The RSSI CAL page has three band targets: **2.4G**, **5G**, **BLE**. Tapping
one starts the calibration *and locks the radio there* — it does not inherit
the band from whatever sweep was running.

That matters. An earlier version took the band from the active sweep, which on
a dual-band Wi-Fi pass alternates every 1.5 seconds, so it was a coin flip;
half the time it selected a band the beacon was not transmitting on and
collected nothing at all. Locking also stops channel hopping: on 5 GHz the
beacon occupies one of up to 25 channels, so hopping them all would leave the
reference heard about 4% of the time — indistinguishable from a dead beacon.

## If it says REFERENCE NOT SEEN

The page names the reference by its SSID or BLE name and samples **only** that
device. If it is not being heard you get:

```
REFERENCE NOT SEEN on 2.4G - 0 samples
```

in red, rather than a silent zero. Check, in order:

1. The beacon is powered and its LED is **blue** (2.4 GHz) or **magenta**
   (5 GHz) — not red, which means the radio refused the requested power.
2. You tapped the band the beacon is actually on. Send `s` to the beacon over
   serial to confirm which.
3. The beacon's serial banner shows an SSID containing `VULP-CAL`. The
   calibration matches on that tag; a renamed beacon will never be found.
4. For 5 GHz: your detector has usable 5 GHz channels. Check SETUP — if it
   reads `5GHz usable 0`, no 5 GHz calibration is possible on this board.

Sampling by name is deliberate. An earlier version sampled the highest-ranked
track on the band, which could be a neighbour's access point — producing a
plausible threshold measured against entirely the wrong transmitter, with no
symptom at all.

## Order of calibration

Calibrate in this order. A measured value always replaces a derived one; the
reverse never happens.

1. **2.4 GHz** — beacon LED blue
2. **5 GHz** — send `5` over serial, LED turns magenta
3. **BLE** — switch the detector to a BLE sweep first

Each one calibrates whichever band the **detector** is currently sweeping, so
start the matching sweep on the CYD before tapping START.

If your reference is 2.4 GHz only (an ESP32, S3, C3 or C6), do 2.4 GHz and
BLE, and Vulpecula derives 5 GHz from the measured 2.4 GHz offset. SETUP marks
it `D` not `M`. That transfers only the error common to both bands — mostly
the receiver chain and the implementation margin — so it is better than an
untouched default and worse than a real measurement.

## Reading the result

SETUP shows provenance per band:

```
gate 2.4 -31/-11M   5 -38/-18M   BLE -58/-43M
M=MEAS D=DERIVED -=DEFAULT   touch cal x2103 y1870
```

A healthy 2.4 GHz result is roughly NEAR −30 to −40 and CONTACT −10 to −20,
with about **16 dB between them** — free space between 2.00 m and 0.30 m is
16.5 dB.

If your measured delta is much *smaller* than 16 dB, the room is reflective
enough to compress near-field readings and your distance estimates will run
optimistic. Note it; it is a property of the room, not a fault.

## Re-calibrate when

- you change the antenna, or move the RF selector resistor
- you change enclosure, especially to anything with metal in it
- you swap boards — do not assume two NM-CYD-C5s match
- readings drift noticeably against the same reference at the same distance

Calibration state per band is shown on SETUP. An uncalibrated build says so in
yellow, on purpose: you should never be unsure whether the numbers you are
looking at were measured or assumed.
