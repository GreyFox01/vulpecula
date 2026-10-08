# Starting a clean GitHub repository

Step by step. Delete this file once you have pushed — it is scaffolding, not
documentation.

> The extracted folder is `vulpecula-project/`, deliberately **not**
> `Vulpecula/`. The old layout gave `Vulpecula\Vulpecula\Vulpecula.ino` —
> two nested folders sharing the name of the file, which is very easy to drag
> or extract into the wrong level of, and was the likely cause of three build
> failures. Only the inner sketch folder has to match its `.ino`; name the
> GitHub repo whatever you like.

---

## 0. Verify the working copy first

Push a broken layout and CI just tells you what you already know.

```powershell
cd C:\priv\vulpecula-project
powershell -ExecutionPolicy Bypass -File tools\check_layout.ps1
```

You want `SKETCH HYGIENE OK`. If a file is reported missing, re-extract the
archive rather than patching it.

---

## 1. Tell git who you are

Skip if you have done this before on this machine.

```powershell
git config --global user.name  "Your Name"
git config --global user.email "you@example.com"
```

The email should be one GitHub knows about, or your commits will not be
attributed to your account.

---

## 2. Discard the bundled history

The archive ships with 18 commits from development. For a clean start, throw
them away:

```powershell
cd C:\priv\vulpecula-project
Remove-Item -Recurse -Force .git
git init -b main
git add -A
git commit -m "Initial commit: Vulpecula bug sweeper for NM-CYD-C5"
```

**Or keep the history** — it records why several non-obvious decisions were
made, and `CHANGELOG.md` references it. To keep it, skip this step entirely;
the repository is already initialised with `main` checked out and a clean
working tree.

---

## 3. Check what you are about to publish

```powershell
git status
git ls-files | Measure-Object -Line
```

Expect **61 tracked files** and nothing reported as untracked or modified.

**If the count is much lower, stop.** A count near 38 means the firmware did
not get committed. That happened once: `.gitignore` contained a bare
`vulpecula/`, intended to exclude the SD card's log directory, and because
Windows git defaults to `core.ignorecase=true` it also matched the
`Vulpecula/` sketch folder — silently excluding every firmware file. Confirm
the firmware is in:

```powershell
git ls-files | Select-String "^Vulpecula/" | Measure-Object -Line
```

That must be **23**. If it is 0, something is excluding the folder:

```powershell
git check-ignore -v Vulpecula/Vulpecula.ino
```

which names the file and line of the pattern responsible. Things that must **not** appear:

- `oui.csv`, `oui.bin` — the IEEE registry, several MB and quickly stale
- `*.csv` sweep logs from the SD card — pseudonymised, but SSIDs and vendor
  strings are verbatim, so a log from a hotel corridor is a list of your
  neighbours' devices
- `build/`, `.pio/`, `*.bin`

`.gitignore` covers all of these. If one shows up anyway, stop and find out
why before pushing.

---

## 4. Create the empty repository on GitHub

**New repository**, and critically: **do not** add a README, `.gitignore` or
licence. All three exist here, and GitHub creating its own gives you a
conflict on the first push.

- **Name** — `vulpecula` reads well
- **Description** — Proximity-gated Wi-Fi + BLE bug sweeper for the ESP32-C5
  Cheap Yellow Display
- **Visibility** — your call. See the note at the end.
- **Topics**, after creation: `esp32`, `esp32c5`, `arduino`,
  `counter-surveillance`, `wifi`, `bluetooth-le`,
  `hidden-camera-detector`, `rf`, `cyd`

---

## 5. Push

```powershell
git remote add origin https://github.com/YOUR-USER/vulpecula.git
git push -u origin main
```

If you are asked to authenticate, use a personal access token rather than
your password — GitHub stopped accepting passwords for git over HTTPS.
Git Credential Manager, bundled with Git for Windows, handles this in a
browser window.

Already pushed something and want to start over? Either delete the repository
on GitHub and recreate it empty, or overwrite it:

```powershell
git push --force origin main
```

---

## 6. Fix the two placeholders

Both are in `README.md`:

- The CI badge URL contains `OWNER/REPO`
- The clone command in **Quick start** contains `OWNER/REPO`

```powershell
git add README.md
git commit -m "Point the CI badge at the real repository"
git push
```

`LICENSE` already reads *Copyright (c) 2026 The Wolf Creek Assembly*, so
nothing to change there.

---

## 7. Watch CI

The **Actions** tab should show three jobs on your push.

**Host checks** finishes in seconds — the eight checks from
`test/run_all.sh`, needing no toolchain.

**Fuzz parsers (guided)** runs libFuzzer against the over-the-air parsers for
two minutes under AddressSanitizer and UndefinedBehaviorSanitizer. A crash
input is uploaded as an artifact.

**Compile for ESP32-C5** is the authoritative one. It installs the pinned
esp32 core and NimBLE and runs a real `arduino-cli compile`. The host job uses
stub headers and cannot see a genuine core problem — and the
prototype-injection bug that broke the first build was invisible to plain
`g++`. First run takes several minutes for the toolchain download, then it is
cached on `esp32-3.3.11-nimble-2.2.3`.

If the compile job fails but host checks pass, the difference is almost always
the real Arduino core versus the stubs. Read its log, not the host job's.

---

## A word on visibility

This is a counter-surveillance tool, and publishing it is a reasonable thing
to do — its value depends on being inspectable, which is also why the gate
maths, the triage rules and the parsers all have tests you can read.

Two things worth thinking about before making it public:

- **Your own sweep logs must never go in.** `.gitignore` handles the default
  paths, but check `git status` each time regardless.
- The original working copy sat under a corporate OneDrive path. Some
  employers claim ownership of work created on their equipment or in their
  systems. Worth a glance at your IP policy rather than my guess about it.
