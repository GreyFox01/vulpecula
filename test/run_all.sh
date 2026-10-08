#!/bin/sh
# All host-side checks. Needs only g++ and python3 - no hardware, no Arduino.
set -e
D=$(dirname "$0")
S="$D/../Vulpecula/src"

echo "### 1/8  sketch folder hygiene - nothing stray for Arduino to compile"
python3 "$D/sketch_hygiene_check.py"

echo "### 2/8  every module compiles, and they all link together"
# Linking is the part that catches a missing or duplicated symbol after a
# refactor; -fsyntax-only on each file would not.
rm -rf /tmp/_vlink && mkdir -p /tmp/_vlink
cp "$S"/*.cpp "$S"/*.h /tmp/_vlink/
sed 's|src/vulpecula.h|vulpecula.h|' "$D/../Vulpecula/Vulpecula.ino" > /tmp/_vlink/_ino.cpp
printf 'void setup();\nint main(){ setup(); return 0; }\n' > /tmp/_vlink/_main.cpp
g++ -std=gnu++17 -I /tmp/_vlink -I "$D/stubs" -Wall -Wextra \
    -o /tmp/_vlink/firmware /tmp/_vlink/*.cpp
echo "    clean, and links"

echo
echo "### 3/8  Arduino prototype injection (the .ino must stay type-free)"
python3 "$D/arduino_prototype_check.py"

echo
echo "### 4/8  screen layout - nothing off-panel or overlapping"
python3 "$D/layout_check.py"

echo "### 5/8  proximity gate, 34 assertions (against the real src/proximity.cpp)"
g++ -std=gnu++17 -I "$S" "$D/gate_test.cpp" "$S/proximity.cpp" -o /tmp/_gate
/tmp/_gate

echo "### 6/8  triage rules, 15 assertions (against the real src/triage.cpp)"
g++ -std=gnu++17 -I "$S" "$D/triage_test.cpp" "$S/triage.cpp" "$S/vendor.cpp" \
    -o /tmp/_triage
/tmp/_triage

echo "### 7/8  calibration reference normalisation, 8 assertions"
g++ -std=gnu++17 "$D/calnorm_test.cpp" -o /tmp/_calnorm
/tmp/_calnorm

echo "### 8/8  fuzz the over-the-air parsers, ASan + UBSan"
g++ -std=gnu++17 -O1 -g -fsanitize=address,undefined -I "$S" \
    "$D/fuzz_parsers.cpp" "$S/parsers.cpp" "$S/proximity.cpp" "$S/vendor.cpp" \
    -o /tmp/_fuzz
/tmp/_fuzz "${FUZZ_ITERS:-20000}"
