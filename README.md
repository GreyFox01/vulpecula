# Vulpecula

**A handheld bug sweeper for the ESP32-C5 Cheap Yellow Display.**

Vulpecula looks for wireless cameras and microphones in a room you occupy — a
hotel room, a short-let, an office, a meeting space. You walk it slowly around
the room; it listens on 2.4 GHz Wi-Fi, 5 GHz Wi-Fi and Bluetooth LE, reports
only the devices close enough to be *in that room*, works out what each one is
where it can, and ranks them so the most concerning thing sits at the top of
the list.

It transmits nothing. No probe requests, no active BLE scanning, no
association. It is a receiver with a screen.

[![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml)

> Replace `OWNER/REPO` in the badge URL once you have pushed.

---

## Contents

- [How it works](#how-it-works) — the one idea the whole tool rests on
- [What it can and cannot find](#what-it-can-and-cannot-find) — read this
- [Hardware](#hardware)
- [Build and flash](#build-and-flash)
- [First run](#first-run)
- [Using it](#using-it) — the sweep workflow
- [Reading the screens](#reading-the-screens)
- [How devices get identified](#how-devices-get-identified)
- [How devices get ranked](#how-devices-get-ranked)
- [Importing your own data](#importing-your-own-data)
- [Security posture](#security-posture)
- [Design notes](#design-notes) — why it is built this way
- [Development](#development)
- [Troubleshooting](#troubleshooting)

---

## How it works

Everything rests on one idea: **the ~2 metre detection gate is a spatial
filter, not a sensitivity limit.**

A detector tuned for maximum range is useless indoors. In a hotel you will
hear 100–300 Bluetooth devices and 50-odd access points — neighbours' phones,
televisions, the building's infrastructure, the lift. A list of all of them is
not an answer, it is homework.

So Vulpecula deliberately throws most of that away. It reports only signals
strong enough to be inside the room with you, and room coverage comes from
*you moving the board*, not from detection range. You sweep a 2 m bubble along
the walls, over the fixtures, across the ceiling, behind the headboard.

Three tiers:

| Tier | Distance | Meaning |
|---|---|---|
| **CONTACT** | ~0.3 m | Confirmation. Open the fixture. |
| **NEAR** | ~2 m | Alert. Worth investigating. |
| **ambient** | beyond | Kept but not shown. Feeds the baseline diff. |

### Why a 2 m gate actually separates rooms

Path loss is steep at close range, and a wall stacks on top of it. At 2.4 GHz
for a +17 dBm camera against the −35 dBm NEAR threshold:

| Scenario | Budget | Result |
|---|---|---|
| in room, 2 m | 17 − 46 | −29 dBm → **NEAR** |
| in room, 4 m | 17 − 52 | −35 dBm → NEAR (borderline by design) |
| next room, 2 m + 12 dB wall | 17 − 46 − 12 | −41 dBm → rejected |
| corridor, 8 m + 6 dB door | 17 − 58 − 6 | −47 dBm → rejected |
| two rooms away, 6 m + 24 dB | 17 − 54 − 24 | −63 dBm → rejected |

Interior hotel demise walls run 10–20 dB at 2.4 GHz, so distance gives about
6 dB of separation and the wall adds 10–20 dB on top. Those five rows are
assertions in `test/gate_test.cpp`, run on every commit.

### The gate is not the only filter

RSSI alone cannot tell a camera 2 m away through a party wall from one 2 m
away in your room, if the first transmits harder. Two further filters do most
of the real work.

**The corridor/room baseline.** Capture what is audible outside the door, then
inside, and flag devices heard *only* inside. This is the filter that actually
separates your neighbour's hardware from yours; the gate is the crude first
pass. It is also why ambient-tier devices are still tracked even though they
are not displayed.

**Behaviour.** A camera streaming video has an unmistakable traffic profile:
sustained, uplink-dominant, from a single non-AP MAC. That needs no vendor
database and it survives MAC randomisation — which matters, because most cheap
cameras randomise.

### Three hardware constraints that shape everything

The ESP32-C5 has one radio and one antenna:

1. **No simultaneous dual-band.** 2.4 and 5 GHz are selected one at a time.
2. **One antenna**, software time-division, no GPIO switching.
3. **Wi-Fi and BLE also share time** by coexistence.

So detection probability at any sweep position is radio-on dwell versus the
target's transmit interval. BLE advertisements range from 20 ms to 10.24 s; a
three-way round-robin at 33% duty would need 30+ seconds per position to cover
a slow advertiser. That is why sweeps are **separate passes** rather than one
combined scan, and why the sweep screen tells you how much radio time a
position has actually had.

---

## What it can and cannot find

**A tool that implies "all clear" is worse than no tool.** Read all of this.

### Invisible to Vulpecula

- **A camera recording to an SD card with its radio off.** This is the largest
  blind spot and no firmware can close it. Many cheap hidden cameras work
  exactly this way. Always also check for lenses.
- **LTE/4G cameras** — they talk to a cell tower, not to anything Vulpecula
  listens on.
- **Wired cameras**, and **analog transmitters**, which are not packets.

### Found, with caveats

- Wi-Fi cameras running their own access point — the easiest case, they beacon
  constantly, on both bands.
- Wi-Fi cameras joined to the room's network — no beacons, so detection leans
  on the traffic profile and the proximity gate.
- BLE cameras, wireless microphones, camera remotes and accessories.

### Things it cannot tell you

- **Whether a device is recording.** A detection means a signal matched a
  profile at a distance. Nothing more.
- **Absence of anything.** No alert is not proof of an empty room.

### Known limits of the ranking

- Devices that rotate MACs or drop their advertised name weaken
  identification. The gate, the baseline diff and the traffic profile still
  work.
- OUI matching has poor coverage on no-name cameras by construction, which is
  why it is weighted low.
- The **CONTACT tier has one documented failure case**: a +23 dBm transmitter
  pressed against thin 8 dB drywall can reach it from the next room. This is
  not fixable by moving the threshold — the ranges genuinely overlap — so the
  test asserts the gap rather than pretending it is closed.

### And until you calibrate

The shipped thresholds are path-loss arithmetic plus an assumed implementation
margin. They are **not** a measurement of your board, your antenna or your
building. Until you run the RSSI calibration every distance and tier on screen
is provisional — and since the list *hides* everything below the NEAR
threshold, it is making exclusion decisions on arithmetic. See
[CALIBRATION.md](CALIBRATION.md). It takes about half an hour.

---

## Hardware

- **RockBase IoT NM-CYD-C5** — ESP32-C5-WROOM-1, 16 MB flash, 8 MB PSRAM,
  2.8" 320×240 ST7789, XPT2046 resistive touch, microSD slot.
- The **-Colorful-Ant** variant is worth the extra. It ships with an IPEX
  pigtail and a dual-band antenna, which gives a characterisable radiation
  pattern to calibrate against. On boards where you fit the connector
  yourself, the 0-ohm RF selector resistor has to be **physically moved** —
  plugging an antenna into the socket does not select it.
- Optional but recommended: any spare ESP32 for the calibration reference
  beacon, and a microSD card for logging and the vendor database.

**5 GHz is the main reason to build this on a C5.** Nearly every other ESP32
detector is 2.4 GHz only, which means a 5 GHz camera is simply invisible to
it. Whether you get the DFS channels (52–144) depends on your regulatory
region; the firmware probes this at runtime and tells you rather than
assuming.

---

## Build and flash

Arduino IDE 2.x. One library.

1. **Boards Manager** → `esp32` by Espressif, **3.3.5 or newer**. Older cores
   have no ESP32-C5 support and will not compile.
2. **Library Manager** → `NimBLE-Arduino` by h2zero, 2.2.x or newer. That is
   the only dependency.
3. **Tools:**

   | Setting | Value |
   |---|---|
   | Board | ESP32C5 Dev Module |
   | USB CDC On Boot | Enabled |
   | Flash Size | 16MB (128Mb) |
   | Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
   | **PSRAM** | **Enabled** — required |
   | Upload Speed | 460800 |

   PSRAM is the one that bites. The track table and the vendor database live
   there, and without it the firmware halts at boot with a red screen and a
   blinking LED.

4. Open `Vulpecula/Vulpecula.ino`, plug into the **ESP32-C5 USB-C port** (not
   the CH340 one), and upload.

Two things that waste an afternoon if you get them wrong:

- **Keep the repo on a local drive.** OneDrive and similar can leave a file as
  a cloud placeholder that looks present in Explorer but reads as empty to the
  compiler, and can take a file lock mid-build.
- **The sketch folder holds exactly one `.ino`.** Arduino compiles every
  source file in a sketch folder and puts the folder on the include path, so a
  stray test file gets built into the firmware, a stray stub header shadows
  the real core headers, and a Vulpecula module copied into
  `reference_beacon/` fails on a missing header that says nothing about the
  real cause.

  `test/sketch_hygiene_check.py` catches all of it. On Windows without Python:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\check_layout.ps1
  ```

  The modules also guard themselves: one compiled outside `src/` emits an
  `#error` explaining where it belongs, rather than a missing-header error.

- **`Vulpecula/src/` must be complete.** A *missing* module fails with
  `src/vulpecula.h: No such file or directory`, which points at a header
  rather than at the gap. `Vulpecula/src/MANIFEST.txt` lists the files that
  have to be there, and both hygiene checks verify a working copy against it.

---

## First run

1. **Colour check.** Three labelled bars and a question: does each bar match
   its letter? Tap **YES**, or **SWAP** to flip the order and see the result
   immediately. The answer is stored in NVS.

   This is a question rather than a `#define` because the failure is invisible.
   A red/blue swap leaves the green bits untouched, so ambient still renders
   green while CONTACT renders blue and NEAR renders teal — the display looks
   plausible while both alert states are wrong, and the only correct tier is
   the one that means "nothing here". This board shipped that way through an
   entire development cycle before anyone looked at the bars.
2. **Touch calibration runs automatically**, because the panel has no usable
   factory mapping and guessed raw limits are not worth having. Tap the centre
   of four crosshairs, then drag on the check screen to confirm the dot sits
   under your finger, and tap ACCEPT. Stored in NVS; you will not see it again
   unless you ask for it from SETUP.
3. **Characterisation data is imported** from the SD card if one is present,
   and commented template files are written on first boot so the card
   documents its own format. See
   [Importing your own data](#importing-your-own-data).
4. **The welcome screen.** Radios stay off until you choose a sweep.

The touch routine derives the axis transposition and both inversions by
measurement rather than asking you to guess which of eight orientations
applies, and extrapolates past the inset crosshairs so the outer 28 px stay
reachable.

---

## Using it

### Sweep modes

| Button | What it does |
|---|---|
| **SWEEP ALL** | Wi-Fi pass across the room, a prompt, then a BLE pass. Two laps. |
| **WI-FI** | 2.4 and 5 GHz, alternating on a 1.5 s band slice. One lap. |
| **BLE** | Passive listen only, full radio. One lap. |
| **WATCH** | Round-robin all three. For leaving the board on a nightstand. |

**SWEEP ALL is two sequential passes, not a simultaneous one**, because the
hardware cannot do simultaneous. The Wi-Fi pass runs, then the screen tells
you to **walk the room again** for BLE. It advances on the `BLE PASS >`
button, or automatically after 150 s of Wi-Fi radio time so an operator who
never taps it still gets both passes.

### The full workflow

1. **Corridor baseline.** SETUP → BASELINE. Read the page, step outside the
   door, tap START, wait out the 60 s countdown.
2. **Room baseline.** Go in, tap `ROOM >`, wait another 60 s.
3. **Tap `APPLY`.** Anything strong in both is infrastructure or a neighbour;
   anything heard only inside is flagged `R` and ranked up. You land on the
   list with the newly promoted devices at the top.
4. **Wi-Fi pass.** Walk the room. Pause at each fixture until the dwell
   readout says you are covered, then tap the screen body to mark the
   position.
5. **BLE pass.** Switch mode and walk it again.
6. **Triage** on LIST, and read the evidence on INFO.

Marking positions matters: it feeds the peak-shape analysis, which
distinguishes a device broadly audible across the room from one that spikes
only when you are against a particular wall.

### Where to point it

Smoke detectors, TV surrounds and soundbars, alarm clocks and radios, USB
chargers and power bricks, mirrors, air vents, picture frames, desk lamps,
tissue boxes, curtain rails, and the whole headboard.

---

## Reading the screens

### SWEEP — what you watch while walking

A tier banner that changes colour, the peak RSSI as a large readout, a
peak-hold bar with the gate thresholds marked as ticks, the strongest device's
identity, and the dwell block:

```
held 3.2 s radio at this position
covers transmit intervals <= 1.07 s
```

That second line is the important one. It is accrued radio time divided by the
packets the gate requires, and it tells you whether you have actually given a
slow BLE advertiser a chance to be heard. **SWEEPING TOO FAST** appears if a
position has had under 1.2 s of radio time.

The RGB LED mirrors the tier — green, amber, red — so you can sweep behind
furniture without looking at the screen.

### LIST — triage

Six rows, three lines each, highest risk first:

```
| CRIT  CAMERA   MY-IPCAM-A41F                     |  ^
|   HANGZHOU HIKVISION DS-2CD2043 *                |
|   2.4G CH6   -28DBM ~1.20M  S R C                |  =
|--------------------------------------------------|
| HIGH  MIC      DJI MIC MINI                      |  v
```

- **Line 1** — risk level, device type, name or SSID
- **Line 2** — vendor and model. A trailing `*` means the vendor string came
  from a WPS attribute off the air rather than an OUI table; treat those as
  exact.
- **Line 3** — band, channel, peak RSSI, estimated distance, evidence letters

Evidence letters: `S` streaming traffic, `R` in-room only per the baseline,
`C` reached contact range, `A` camera access point, `N` name match.

**The list shows only devices consistent with being in this room.** Ambient
devices are still tracked for the baseline diff, just not displayed. The
header says how many are hidden, and tapping it toggles SHOW ALL — nothing is
concealed silently.

Scroll with the arrows on the right. They page by five rows, keeping one for
continuity, with a position thumb between them.

### INFO — why

Tap any row. You get the full device name on its own line, the triage rule
that fired, and each piece of evidence behind it. A verdict you cannot audit
is a verdict you should not trust, so the rule is always shown.

### SETUP

Diagnostics and six controls: baseline, RSSI calibration, touch calibration,
new session, import, wipe logs, stop radios. The diagnostics block answers
most "why is it not working" questions at a glance — usable 5 GHz channels,
frame and advert counters with drops, SD state, the active gate thresholds,
and whether touch and RSSI are calibrated or running on defaults.

---

## How devices get identified

Four tiers of evidence, and the tier decides how much the ranking trusts it.

| Tier | Source | Why it is strong or weak |
|---|---|---|
| **A** | Self-declared: WPS Primary Device Type, BLE Appearance, SIG microphone service UUIDs | The device states what it is, in cleartext |
| **B** | Behaviour: sustained uplink-dominant traffic | No database needed, survives MAC randomisation |
| **C** | Advertised name or SSID pattern | Vendor naming conventions |
| **D** | OUI belongs to a camera vendor | Weakest — see below |

### WPS is the standout

A Wi-Fi device in access-point mode usually broadcasts WPS information
elements in every beacon, in cleartext, containing:

| Attribute | ID | Gives you |
|---|---|---|
| Manufacturer | 0x1021 | vendor name as a **string**, no lookup needed |
| Model Name | 0x1023 | e.g. `IPC-HDW1230T` |
| Device Name | 0x1011 | the vendor's own label |
| Primary Device Type | 0x1054 | self-declared category |

Primary Device Type is 8 bytes: a 2-byte CategoryID, a 4-byte OUI
(`00 50 F2 04` for predefined values), and a 2-byte SubcategoryID. **Camera is
category 4.** A device announcing itself as a security camera in a plaintext
beacon beats every heuristic in the firmware.

### BLE

**Appearance** (AD type 0x19) distinguishes **Audio Source** — a microphone —
from **Audio Sink** — a speaker. That distinction is load-bearing: conflating
them would put every Bluetooth speaker at the top of your list. Microphone
Control (0x184D) and Audio Input Control (0x1843) service UUIDs mean the
device has a microphone in it.

BLE advertisements also often carry a calibrated transmit-power reference — AD
type 0x0A, an iBeacon measured power, or Eddystone ranging data. Where one
exists, distance is computed from path loss directly instead of gating raw
RSSI. This matters more than it sounds: consumer BLE transmit power spans about
−20 to +8 dBm, so without a reference 28 dB of unknown at the source swamps
the 12 dB of wall margin the gate depends on. The suite verifies a −20 dBm
advertiser is correctly admitted at 2 m and rejected at 5 m, which raw RSSI
cannot do.

> The exact BLE Appearance category numbers and WPS subcategory numbers come
> from specification knowledge rather than a capture. The WPS *category* is
> what drives typing, so an unrecognised subcategory still yields the right
> answer, and the raw Appearance value is shown on INFO so you can check it.

### Vendor names

Two sources. WPS Manufacturer gives an exact string off the air, marked `*` in
the list. Otherwise an OUI lookup against the imported database, falling back
to a small compiled table if no card is present.

That fallback table is **deliberately tiny**. A wrong OUI prefix does not fail
quietly — it puts a confident vendor name, a device type and a risk label next
to innocent hardware, and a label that is wrong once costs the operator's
trust permanently.

---

## How devices get ranked

Risk combines two independent questions: how confident are we this is a
recording device, and how confident are we it is in *this* room. The list is
ordered by level, but the rule that fired is recorded and shown, so a label
can be judged rather than taken on faith.

The policy is **balanced**: strong type evidence inside the gate is enough for
CRITICAL. Contact range and a corridor baseline are not required.

| Level | Fires when |
|---|---|
| **CRIT** | Recorder at contact range; or tier A/B recorder inside the gate; or a recorder that is NEAR and in-room only |
| **HIGH** | Tier C/D recorder inside the gate; streaming at contact; in-room only; or anything unidentified at contact |
| **MED** | Inside the gate with weaker evidence; or a declared recorder *outside* the gate |
| **LOW** | No proximity and no type evidence |

Three rules are load-bearing, and each has a test:

- **Anything outside the gate caps at MEDIUM**, however perfect its signature.
  A declared camera in the next room must not crowd out the thing in the smoke
  detector.
- **Microphones are not down-ranked** relative to cameras. Audio needs no line
  of sight and works through fabric and plastic.
- **Declared speakers and phones are never recorders**, so a soundbar cannot
  reach CRITICAL.

Levels have promotion and demotion hysteresis — 1.5 s up, 6 s down — and the
list re-sorts on a 1 Hz cadence, so labels and row order do not churn.

---

## Importing your own data

Vendor and device-type data lives on the SD card and is imported into PSRAM,
so adding brands or rules needs a text editor and a card reader rather than a
reflash. With 8 MB of PSRAM the whole IEEE registry fits in RAM — about 35,000
assignments at 32 bytes is 1.1 MB — so this replaces the compiled tables
rather than supplementing them.

Three files under `/vulpecula/`, and **the split between them is the point**:

| File | Role |
|---|---|
| `oui.csv` | **Facts.** The IEEE registry as downloaded. Prefix → organisation name. Nothing in it says what a device *does*. |
| `rules.csv` | **Judgement.** Organisation-name keyword → device type and confidence weight, applied once at import. This is where "Hikvision makes cameras" lives, and the file you will actually tune. |
| `names.csv` | Patterns matched at runtime against SSIDs and BLE names, for devices whose prefix says nothing useful — which is most no-name cameras. |

Keeping facts and judgement apart means you can replace the registry wholesale
when it updates without re-deciding every brand. Mixing them would make every
refresh destroy your tuning.

Drop the real registry over `oui.csv`:

```
https://standards-oui.ieee.org/oui/oui.csv
```

Either the official four-column export or a plain `prefix,vendor` file works.
The parser handles the quoting the registry uses for organisation names
containing commas, which a naive split corrupts.

A parsed registry is cached as `oui.bin` — a flat sorted array, binary-searched
on lookup — because parsing 35,000 CSV lines off SD is slow enough to be
irritating. **SETUP → IMPORT** forces a full reparse after you edit the card.

Lookups are bounded deliberately: a hotel puts 300 ambient devices in the
track table, and resolving all of them would spend the whole budget on things
that can never alert. Only tracks that crossed the gate, or that already carry
some evidence, get resolved.

`tools/make_vendor_table.py` filters a registry by vendor keyword and emits
paste-ready entries if you would rather extend the compiled fallback instead.

---

## Security posture

A counter-surveillance tool is a strange thing to be careless with. What is
and is not done:

- **Receive only.** No probe requests, no active BLE scanning, no association,
  nothing transmitted.
- **The sweeper's own MAC is randomised every boot** to a locally-administered
  address. In promiscuous mode the MAC layer can still ACK, and a tool that
  broadcasts a constant identifier is a contradiction — anyone running their
  own sweep would otherwise see the same device across rooms and dates. BLE
  needs no equivalent while scanning is passive, because the controller
  transmits nothing at all.
- **The parsers that eat hostile data are isolated and fuzzed.**
  `src/parsers.cpp` handles every attacker-controlled byte — 802.11
  information elements, WPS attributes, BLE advertisements, the import files —
  depends on nothing from Arduino, and is fuzzed under AddressSanitizer and
  UndefinedBehaviorSanitizer on every commit, with a corpus of the shapes that
  break length-prefixed parsers plus a truncation sweep at every byte offset.
- **Logs can be wiped.** SETUP → WIPE LOGS deletes every sweep file and
  restarts the session with a new salt. MACs are written as a session-salted
  SHA-256 pseudonym whose salt is random per boot and never persisted, but
  SSIDs, vendor and model strings are verbatim, so a lost card is a record of
  where you swept and what was there. The wipe is an unlink, **not** a secure
  erase — destroy the card if someone imaging the flash is in your threat
  model.
- **Logs hold frame metadata only.** Never payloads, and nothing from an
  encrypted network beyond header fields.
- **CI dependencies are pinned**, because a library update must not change
  firmware behaviour between two identical commits.
- **The ESP32 Wi-Fi blob is attack surface you cannot audit.** Receive-only is
  not risk-free: you are exposed to any bug in Espressif's closed-source
  `esp_wifi` while parsing hostile beacons. Keep the core current. The
  firmware never associates, which is the one mitigation available.
- **Flash encryption and secure boot** are supported by the C5 and are *not*
  enabled. Worth considering if your threat model includes someone tampering
  with the detector to blind it. Both are one-way and carry a bricking risk.

**Sweep spaces you occupy or are authorised to assess.** Radio, privacy and
surveillance law varies by jurisdiction, and none of this is legal advice.

---

## Design notes

Decisions that look arbitrary until you know why.

**A scan can be stopped from anywhere.** The sweep screen carries a STOP in
its banner, SETUP has STOP RADIOS, the serial console takes `x`, and tapping
**HOME** stops the radios on the way back to the menu — the welcome screen is
the nothing-running state by design, so arriving there with a sweep still live
was inconsistent, and meant a scan could be left running indefinitely by
accident. The welcome screen says which it is: `RADIOS OFF` or
`RADIOS RUNNING`.

**Nothing scans until you start it.** `setup()` enables no radio at all and
boots to idle on the welcome screen. Even the 5 GHz capability probe is
deferred to the first Wi-Fi sweep — it is receive-only and processes no
frames, but it brings the PHY up and hops 25 channels, and the guarantee is
easier to trust if boot genuinely touches nothing. Headless builds
(`SERIAL_ONLY 1`) also wait for a command.

**The firmware lives in `src/`, not in the `.ino`.** Arduino generates a
forward declaration for every top-level function in a sketch and injects the
set at the first function definition it finds, so a prototype naming a
later-declared type fails to parse and the errors cascade into dozens of
misleading messages. Three builds broke that way. Code in `src/` is not
preprocessed like that. The `.ino` is 354 lines: `setup()`, `loop()` and the
serial console. See [`Vulpecula/src/README.md`](Vulpecula/src/README.md) for
the module layout and the pure/hardware split.

**Nothing is cleared per frame.** Chrome — banners, labels, button frames, the
tab bar — paints once when the screen changes. Values live in fields that pad
to a fixed character width, so a shortening value erases its own tail, and
that diff per character cell so only changed cells are written. A steady
reading costs zero SPI traffic.

The first build cleared a full region and repainted it 4.5 times a second on
every screen, which read as flicker. The clears were never needed — the glyph
renderer already writes background pixels for the off-bits of every cell.
Removing them also cut SPI traffic by roughly an order of magnitude, which
reduces contention with SD logging on the shared bus.

**The display is upper case throughout.** A 6×8 cell has no room below the
baseline, so `g j p q y` were clipped and read as `o i b a v`. Shrinking the
cap height to buy two descender rows would make everything harder to read at
arm's length, which is the wrong trade for a tool you use while reaching
behind a headboard. Folding happens at the single point every glyph passes
through, so it covers captured strings too — but **the SD log records them
exactly as received**, because SSID case is evidence. Set `UI_ALL_CAPS 0` to
revert.

**Display pin provenance.** `TFT_DC = 24` is **not** in the vendor pinout
table, which omits it. It comes from RockBase-iot/NM-CYD-C5 issue #3, filed by
someone who brought the board up and published a working config; same source
for `TFT_RST = -1` and SPI at 20 MHz rather than 40. That issue also documents
two colour traps this firmware handles: the panel wants inversion **off**
(TFT_eSPI's ST7789 table sends INVON unconditionally, which is why red renders
as yellow or cyan), and colour order may need to be BGR.

The display is driven directly over the stock SPI library in about 200 lines
rather than through TFT_eSPI, because upstream TFT_eSPI has no ESP32-C5
backend and needs the vendor's processor files plus target-guard edits — a
managed-library hand-patch that gets wiped on every update.

**The vendor wiki contradicts itself** on touch, calling the panel capacitive
while also listing an XPT2046, which is a resistive controller. It is driven
as resistive, matching the vendor README and every community project.

**All three SPI peripherals take a mutex.** Display, touch and SD share the
bus and are driven from two tasks at different priorities.
`SPI.beginTransaction()` does not serialise across tasks, and a preempted
transfer leaves the display mid-command with CS asserted — corrupted rows, a
frozen panel, or SD writes silently returning garbage.

---

## Development

```sh
bash test/run_all.sh
```

Eight checks, needing only `g++` and `python3` — no hardware, no Arduino
toolchain. On Windows use Git Bash or WSL. CI runs these plus a
coverage-guided libFuzzer run and a real `arduino-cli` compile for the
ESP32-C5.

1. **Sketch folder hygiene** — Arduino compiles every source file in a sketch
   folder and puts it on the include path, so a stray test file or a host stub
   header breaks the build in ways whose errors point nowhere near the cause.
2. **Every module compiles and they all link.** Linking is what catches a
   missing or duplicated symbol after a refactor; per-file syntax checks do
   not.
3. **Arduino prototype injection** — the `.ino` must stay type-free. `src/` is
   immune; the `.ino` is not, so this stops anything type-dependent drifting
   back in. A plain `g++` build cannot catch this class of bug at all.
4. **Screen layout** — every `rect_t` and literal string fits inside 320×240
   without overlapping the tab bar, a status line, or *another control on the
   same screen*. Nothing at compile time knows how big the panel is, so a
   button at y=220 compiles perfectly and is unreachable, and one dropped on
   top of another passes every other check.
5. **Proximity gate** — 34 assertions against the real `src/proximity.cpp`.
6. **Triage rules** — 15 assertions against the real `src/triage.cpp`.
7. **Calibration reference normalisation** — 8 assertions, including that the
   correction sign is right. Backwards, it doubles the error instead of
   cancelling it, and the numbers still look plausible.
8. **Fuzz the over-the-air parsers** under ASan and UBSan.

Tests 5, 6 and 8 link the **real implementation**, which is possible because
`src/pure.h` is hardware-free. They used to slice code out of the sketch with
string markers, a workaround that had to be repaired whenever a section moved
and could silently test a stale copy.

[CONTRIBUTING.md](CONTRIBUTING.md) lists five gotchas that have each already
broken this project. Worth reading before a first change.

### Repository layout

```
Vulpecula/Vulpecula.ino        setup(), loop(), serial console
Vulpecula/src/                 the firmware; pure core split from hardware
Vulpecula/src/MANIFEST.txt     the file list a working copy is checked against
START-HERE.txt                 layout rules and first-boot steps
reference_beacon/              calibration transmitter for a spare ESP32
tools/make_vendor_table.py     accurate OUI entries from the IEEE registry
test/run_all.sh                the seven host checks
CALIBRATION.md                 the two experiments to do before trusting it
CONTRIBUTING.md                the gotchas
CHANGELOG.md                   what changed, and the bugs found along the way
```

### Most useful contributions

Some things cannot be tested on a host, and several values came from
specification knowledge rather than a capture:

- WPS Primary Device Type subcategory numbers under WSC 2.0
- BLE Appearance category numbers, especially Audio Source versus Audio Sink
- Whether Microphone Control (0x184D) and Audio Input Control (0x1843) appear
  on real wireless microphones
- Measured RSSI at 2.00 m and 0.30 m for your board and antenna, and whether a
  reference reads ambient through an interior wall

---

## Troubleshooting

**The screen stays dark.** Set `SERIAL_ONLY 1` in `src/config.h`. The whole
detector then runs headless over the serial monitor at 115200 with single-key
commands, so you can verify the radio side before fighting the panel:

```
a = sweep all    w = Wi-Fi          b = BLE         t = watch
n = next pass    m = mark position  p = baseline phase
l = list tracks  c = RSSI cal       x = stop radios
```

Then try, in order: `PANEL_DRIVER 1` (ILI9341 — the vendor says some units
ship that way), `PANEL_BGR 0`, `PANEL_INVERT 1`, `SPI_HZ_LCD 10000000`, then
`PIN_LCD_DC` 21 / 22 / 20. Issue #3 is one report, not a datasheet.

**Colours are wrong.** Red and blue swapped → SETUP → TOUCH CAL re-runs the
colour check; tap SWAP. Everything inverted or washed out is a different
fault: `PANEL_INVERT 1` in `src/config.h`.

**Touch lands in the wrong place.** SETUP → TOUCH CAL. If it reports failure
you tapped the same spot twice, or missed the crosshair centres.

**`5GHz usable 0`** on SETUP means the band-mode call failed — a core or
regulatory problem, not a firmware one. Check the esp32 core is 3.3.5+.

**`ident drop` climbing fast** means the management-frame queue is
overflowing. Raise `IE_QUEUE_LEN` in `src/config.h`.

**Nothing in the list.** Expected, if nothing is within ~2 m. Tap the list
header for SHOW ALL to confirm the radio is hearing anything at all, and check
the frame and advert counters on SETUP.

**`Platform 'esp32:esp32' not found`** — the board package is not installed.
Boards Manager → `esp32`.

**Compile errors naming a test file** — a test file is in the sketch folder.
Run `python3 test/sketch_hygiene_check.py`.

---

## Licence

MIT — see [LICENSE](LICENSE). Copyright © 2026 The Wolf Creek Assembly.

The signature tables are leads requiring field confirmation, not proof of
anything.
