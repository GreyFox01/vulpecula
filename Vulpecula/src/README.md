# src/ — the firmware

Arduino compiles a sketch's `src/` folder recursively, and — the reason for
the split — it does **not** run the prototype-injection preprocessor on files
here. That preprocessor generates a forward declaration for every top-level
function in a `.ino` and injects the set at the first function definition it
finds, so a prototype naming a later-declared type fails to parse and the
errors cascade into dozens of misleading messages. Three separate builds broke
that way before the move.

Code here is ordinary C++ with ordinary headers and ordinary declaration order.

## The layering that matters

| | |
|---|---|
| **`pure.h`** | The hardware-free core. Compiles on a host with **no Arduino headers, no stubs and no board**. |
| **`vulpecula.h`** | `pure.h` plus everything that touches the board. |

Pure modules must not include `vulpecula.h`. That constraint is what makes the
tests meaningful: `test/gate_test.cpp` and `test/triage_test.cpp` link against
the real `proximity.cpp` and `triage.cpp`, and `test/fuzz_parsers.cpp` links
against the real `parsers.cpp`. They used to slice code out of the sketch with
string markers, which had to be repaired whenever a section banner moved and
could silently test a stale copy.

### Pure

| Module | Contents |
|---|---|
| `proximity.cpp` | the gate: tiers, hysteresis, peak/median, BLE ranging |
| `triage.cpp` | the risk rule table |
| `classify.cpp` | evidence weighting, streaming heuristic |
| `vendor.cpp` | device-type mappings, WPS and BLE Appearance decode, fallback tables |
| `parsers.cpp` | **all input parsing** — see below |

### Hardware

`display.cpp`, `touch.cpp`, `tracks.cpp`, `wifi_cap.cpp`, `ble_cap.cpp`,
`sched.cpp`, `calib.cpp`, `logsd.cpp`, `importdb.cpp`, `nvsstore.cpp`,
`ui.cpp`.

## parsers.cpp is isolated on purpose

It is the only code in the project that processes data an **attacker fully
controls**: 802.11 information elements, WPS attributes nested inside them,
BLE advertisement structures, and the CSV import files. Every length field in
those comes from the transmitter, and a hostile beacon is the cheapest
possible attack on a device whose job is to sit and listen to beacons.

Because it depends on nothing from Arduino it is fuzzed directly under ASan
and UBSan on every commit, with a hand-written corpus of the shapes that break
length-prefixed parsers plus a truncation sweep at every byte offset. Keep it
that way: if something you add here needs `Arduino.h`, it belongs in a
hardware module.
