---
name: Bug report
about: Something does not work as described
labels: bug
---

## What happened

<!-- What you expected, and what you got instead. -->

## Hardware

- Board variant: <!-- NM-CYD-C5 / NM-CYD-C5-Colorful-Ant / other -->
- Antenna: <!-- PCB or external. If external, was the 0-ohm RF selector moved? -->
- esp32 core version: <!-- Boards Manager -> esp32. Must be >= 3.3.5 -->
- NimBLE-Arduino version:

## SETUP screen readout

The diagnostics line on SETUP answers most questions at once. Please paste or
photograph it:

```
5GHz usable ___ ch ___
WiFi frames ___ drop ___  ident ___ drop ___
BLE adverts ___  with TX ref ___
SD ___
gate N/C 2.4 ___/___  5 ___/___  BLE ___/___
touch ___  raw x___ y___ z___  RSSI ___
```

Particularly relevant:

- **5GHz usable 0** means the band-mode call failed — a core or regulatory
  problem, not a firmware one.
- **ident drop climbing fast** means the management-frame queue is
  overflowing; try raising `IE_QUEUE_LEN`.
- **touch DEFAULT** means the guided calibration has never completed.
- **RSSI DEFAULT** means the gate thresholds are calculated, not measured, so
  any distance or tier reading is provisional.

## Host checks

Please run these and paste the output — they need only `g++` and `python3`,
and they localise most problems without hardware:

```sh
bash test/run_all.sh
```

## Serial log

Set `SERIAL_ONLY 1` if the display is the problem. Serial commands:
`a` sweep all, `w` Wi-Fi, `b` BLE, `t` watch, `n` next pass, `m` mark,
`p` baseline phase, `l` list, `c` RSSI cal, `x` stop.

```
paste serial output here
```
