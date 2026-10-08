#!/usr/bin/env python3
"""
Check the sketch folders contain nothing but their own .ino.

WHY THIS EXISTS

The Arduino IDE compiles EVERY source file it finds in a sketch folder, and
adds that folder to the include path. So a stray file there is not inert:

  * a copied-in host test gets compiled into the firmware and fails on a
    placeholder include that has nothing to do with the firmware
  * a second .ino gives duplicate setup() and loop()
  * and worst, test/stubs/ holds fake Arduino.h, SD.h and WiFi.h headers for
    host builds. On the sketch include path those can shadow the real ESP32
    core headers, and the resulting errors point nowhere near the cause

All three have happened. This catches them before a confusing build does.

    python3 test/sketch_hygiene_check.py
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def find_root():
    """Locate the project root by searching, not by assuming.

    This used to be os.path.join(HERE, ".."), which assumes the script sits
    one level below the root. Run a stray copy from somewhere else and every
    project folder is reported missing - which says the files are gone when
    really the checker is lost. That happened.
    """
    seen = []
    for start in (HERE, os.getcwd()):
        d = start
        for _ in range(6):
            seen.append(d)
            parent = os.path.dirname(d)
            if parent == d:
                break
            d = parent
    for c in dict.fromkeys(seen):
        if os.path.isfile(os.path.join(c, "Vulpecula", "Vulpecula.ino")):
            return c
    for c in dict.fromkeys(seen):
        if (os.path.isfile(os.path.join(c, "START-HERE.txt")) or
                os.path.isdir(os.path.join(c, "Vulpecula"))):
            return c
    return None


ROOT = find_root()
if ROOT is None:
    print("\nCANNOT FIND THE PROJECT ROOT\n")
    print(f"  looked upward from script   : {HERE}")
    print(f"  looked upward from directory: {os.getcwd()}")
    print("\n  A project root contains Vulpecula/Vulpecula.ino and")
    print("  START-HERE.txt. Alongside this script:\n")
    look = os.path.dirname(HERE)
    if os.path.isdir(look):
        for e in sorted(os.listdir(look)):
            kind = "DIR " if os.path.isdir(os.path.join(look, e)) else "file"
            print(f"    {kind} {e}")
    print("\n  Most likely you are running a stray copy of this script that")
    print("  is not inside a project tree. Run the one in the extracted")
    print("  archive's test/ folder.\n")
    sys.exit(2)

# Arduino treats only these subfolders of a sketch as special; anything else
# in there is compiled or on the include path.
ALLOWED_SUBDIRS = {"src", "data"}
# The firmware lives in src/. It must still hold no second .ino and no
# host stub headers, which would shadow the real core headers.
STUB_NAMES = {"Arduino.h", "SPI.h", "SD.h", "WiFi.h", "esp_wifi.h",
              "NimBLEDevice.h", "nvs.h", "nvs_flash.h"}
SOURCEY = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".ino", ".S", ".s"}

problems = []
notes = []


def check_sketch(folder):
    name = os.path.basename(folder)
    expected = name + ".ino"
    entries = sorted(os.listdir(folder))

    if expected not in entries:
        problems.append(f"{name}/: missing {expected} — Arduino requires the "
                        f".ino to match its parent folder name")

    for e in entries:
        p = os.path.join(folder, e)
        if os.path.isdir(p):
            if e in ALLOWED_SUBDIRS:
                for f in sorted(os.listdir(p)):
                    if f.endswith(".ino"):
                        problems.append(f"{name}/{e}/{f}: a .ino under src/ is "
                                        f"compiled as part of the sketch")
                    if f in STUB_NAMES:
                        problems.append(f"{name}/{e}/{f}: host stub header on "
                                        f"the sketch include path — it will "
                                        f"shadow the real core header")
                continue
            if e not in ALLOWED_SUBDIRS:
                problems.append(f"{name}/{e}/: unexpected subfolder. Arduino "
                                f"only special-cases src/ and data/; move it out")
            continue
        if e == expected:
            continue
        ext = os.path.splitext(e)[1]
        if ext in SOURCEY:
            why = ("a second sketch gives duplicate setup()/loop()"
                   if ext == ".ino" else
                   "fake host headers can shadow the real core headers"
                   if ext in (".h", ".hpp") else
                   "it gets compiled into the firmware")
            problems.append(f"{name}/{e}: must not be in the sketch folder — "
                            f"{why}")


def check_gitignore():
    """No .gitignore pattern may exclude a file the project needs.

    WHY THIS EXISTS

    .gitignore contained a bare "vulpecula/", added to exclude the firmware's
    SD log directory. On Windows git defaults to core.ignorecase=true, so it
    also matched the Vulpecula/ sketch folder and silently excluded the entire
    firmware: 38 files committed instead of 61, no warning, and a repository
    that looked fine until someone tried to build it.

    It does not reproduce on a case-sensitive filesystem, which is how it got
    past every other check here. So this test does not rely on the local
    filesystem's behaviour - it compares patterns against required paths
    case-INSENSITIVELY, the way Windows would.
    """
    gi = os.path.join(ROOT, ".gitignore")
    if not os.path.isfile(gi):
        notes.append(".gitignore absent, nothing to check")
        return

    pats = []
    for line in open(gi):
        line = line.strip()
        if line and not line.startswith("#"):
            pats.append(line)

    # Path components that must never be excluded.
    required = {"Vulpecula", "src", "reference_beacon", "test", "tools",
                "stubs", "freertos", "mbedtls"}
    required_files = {"Vulpecula.ino", "reference_beacon.ino", "README.md",
                      "LICENSE", "MANIFEST.txt", "run_all.sh"}

    for p in pats:
        if p.startswith("!"):
            continue
        bare = p.strip("/")
        if "*" in bare or "?" in bare:
            continue          # globs are matched by git, not by name equality
        low = bare.lower()
        for r in required:
            if low == r.lower():
                problems.append(
                    f'.gitignore pattern "{p}" matches the required folder '
                    f'"{r}". On a case-insensitive filesystem this silently '
                    f'excludes it. Anchor the pattern or rename it.')
        for r in required_files:
            if low == r.lower():
                problems.append(
                    f'.gitignore pattern "{p}" matches the required file '
                    f'"{r}".')

    notes.append(f".gitignore: {len(pats)} patterns, none collide with a "
                 f"required path")


def check_manifest():
    """The manifest is the contract a working copy is verified against, so it
    has to match what is actually in src/. A stale manifest would let a real
    gap through."""
    src = os.path.join(ROOT, "Vulpecula", "src")
    man = os.path.join(src, "MANIFEST.txt")
    if not os.path.isdir(src):
        problems.append("Vulpecula/src/ is missing — the firmware lives there")
        return
    if not os.path.isfile(man):
        problems.append("Vulpecula/src/MANIFEST.txt is missing")
        return

    listed = set()
    for line in open(man):
        line = line.strip()
        if line and not line.startswith("#"):
            listed.add(line)
    actual = set(f for f in os.listdir(src)
                 if os.path.isfile(os.path.join(src, f)) and f != "MANIFEST.txt")

    for f in sorted(listed - actual):
        problems.append(f"MANIFEST lists {f}, which is not in src/")
    for f in sorted(actual - listed):
        problems.append(f"src/{f} is not in MANIFEST.txt — add it, or a "
                        f"working copy missing it will not be detected")
    notes.append(f"manifest: {len(listed)} files, matches src/")


def main():
    sketches = []
    for e in sorted(os.listdir(ROOT)):
        p = os.path.join(ROOT, e)
        if os.path.isdir(p) and os.path.exists(os.path.join(p, e + ".ino")):
            sketches.append(p)
        elif os.path.isdir(p) and any(f.endswith(".ino")
                                      for f in os.listdir(p)):
            sketches.append(p)

    if not sketches:
        print("  no sketch folders found — is this the repository root?")
        return 1

    check_gitignore()
    check_manifest()

    for s in sketches:
        check_sketch(s)
        n = len([f for f in os.listdir(s)
                 if os.path.splitext(f)[1] in SOURCEY])
        print(f"  note: {os.path.basename(s)}/ holds {n} source file(s)")

    for n in notes:
        print(f"  note: {n}")
    print()
    if problems:
        for p in problems:
            print(f"  FAIL: {p}")
        print(f"\nSKETCH HYGIENE FAILED ({len(problems)} problem"
              f"{'' if len(problems) == 1 else 's'})\n")
        return 1
    print(f"SKETCH HYGIENE OK — {len(sketches)} sketch folder(s), each holding "
          f"only its own .ino\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
