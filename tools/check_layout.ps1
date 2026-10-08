# check_layout.ps1 - sketch folder hygiene, for Windows without Python.
#
# Arduino compiles EVERY source file in a sketch folder and puts that folder
# on the include path. So a stray file there is not inert:
#
#   * a Vulpecula module copied in gets built as part of the wrong sketch,
#     and fails on a missing header that says nothing about the real problem
#   * a second .ino gives duplicate setup() and loop()
#   * a host stub header (test\stubs\Arduino.h, SD.h, WiFi.h) shadows the
#     real ESP32 core header, and those errors point nowhere near the cause
#
# Run it from anywhere - it finds the project root itself:
#     powershell -ExecutionPolicy Bypass -File tools\check_layout.ps1

$ErrorActionPreference = "Stop"
$problems = @()

# ---------------------------------------------------------------------------
# Locate the project root by SEARCHING for it, not by assuming.
#
# This used to be Split-Path -Parent $PSScriptRoot, which assumes the script
# sits exactly one level below the root. Run it from a copy left in an old
# tree, or from a different folder, and it reports every project folder as
# missing - which says the files are gone when really the checker is lost.
#
# Walk up from the script, then from the current directory, looking for the
# marker that only a real project root has.
# ---------------------------------------------------------------------------
function Find-ProjectRoot {
    $candidates = @()
    foreach ($start in @($PSScriptRoot, (Get-Location).Path)) {
        if (-not $start) { continue }
        $d = $start
        for ($i = 0; $i -lt 6 -and $d; $i++) {
            $candidates += $d
            $d = Split-Path -Parent $d
        }
    }
    foreach ($c in ($candidates | Select-Object -Unique)) {
        if (Test-Path (Join-Path $c "Vulpecula\Vulpecula.ino")) { return $c }
    }
    # Second pass: a root that has the folders but is missing the .ino is
    # still the root, and saying so is more useful than saying "not found".
    foreach ($c in ($candidates | Select-Object -Unique)) {
        if ((Test-Path (Join-Path $c "START-HERE.txt")) -or
            (Test-Path (Join-Path $c "Vulpecula"))) { return $c }
    }
    return $null
}

$root = Find-ProjectRoot

if (-not $root) {
    Write-Host ""
    Write-Host "CANNOT FIND THE PROJECT ROOT" -ForegroundColor Red
    Write-Host ""
    Write-Host "  Looked upward from:"
    Write-Host "    script   : $PSScriptRoot"
    Write-Host "    directory: $((Get-Location).Path)"
    Write-Host ""
    Write-Host "  A project root contains Vulpecula\Vulpecula.ino and"
    Write-Host "  START-HERE.txt. Here is what is actually alongside the script:"
    Write-Host ""
    $look = if ($PSScriptRoot) { Split-Path -Parent $PSScriptRoot } else { (Get-Location).Path }
    if (Test-Path $look) {
        Get-ChildItem $look | ForEach-Object {
            $kind = if ($_.PSIsContainer) { "DIR " } else { "file" }
            Write-Host "    $kind $($_.Name)"
        }
    } else {
        Write-Host "    (cannot list $look)"
    }
    Write-Host ""
    Write-Host "  Two common causes:" -ForegroundColor Yellow
    Write-Host "    1. Windows Explorer extracted the archive one level deeper"
    Write-Host "       than expected, giving vulpecula-project\vulpecula-project\."
    Write-Host "       Use the INNER folder."
    Write-Host "    2. You are running a copy of this script left behind in an"
    Write-Host "       older, partly-deleted tree. Delete that tree and extract"
    Write-Host "       the archive fresh."
    Write-Host ""
    exit 2
}

Write-Host ""
Write-Host "  project root: $root"

# --- src/ must be complete -------------------------------------------------
# A MISSING module fails with "src/vulpecula.h: No such file or directory",
# which points at a header rather than at the gap. MANIFEST.txt is the list of
# what has to be there.
$src = Join-Path $root "Vulpecula\src"
$man = Join-Path $src "MANIFEST.txt"
if (-not (Test-Path $src)) {
    $problems += "Vulpecula\src\ : MISSING. The firmware lives here - re-extract it from the download."
} elseif (-not (Test-Path $man)) {
    $problems += "Vulpecula\src\MANIFEST.txt : missing, cannot verify the module list"
} else {
    $listed = Get-Content $man | Where-Object { $_.Trim() -and -not $_.StartsWith("#") } | ForEach-Object { $_.Trim() }
    $actual = Get-ChildItem $src -File | Where-Object { $_.Name -ne "MANIFEST.txt" } | ForEach-Object { $_.Name }
    foreach ($f in $listed) {
        if ($actual -notcontains $f) {
            $problems += "Vulpecula\src\$f : MISSING - re-extract it from the download"
        }
    }
    Write-Host "  note: src\ holds $($actual.Count) of $($listed.Count) expected files"
}

$sketches = @("Vulpecula", "reference_beacon")
foreach ($name in $sketches) {
    $dir = Join-Path $root $name
    if (-not (Test-Path $dir)) {
        $problems += "$name\ : folder missing"
        continue
    }

    $expected = "$name.ino"
    if (-not (Test-Path (Join-Path $dir $expected))) {
        $problems += "$name\ : missing $expected - Arduino requires the .ino to match its parent folder name"
    }

    $sourcey = @(".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".ino", ".s")
    Get-ChildItem $dir -File | ForEach-Object {
        if ($_.Name -eq $expected) { return }
        if ($sourcey -contains $_.Extension.ToLower()) {
            $why = if ($_.Extension -eq ".ino") {
                "a second sketch gives duplicate setup()/loop()"
            } elseif ($_.Extension -in ".h", ".hpp") {
                "a header here can shadow the real core header"
            } else {
                "it gets compiled into this sketch"
            }
            $problems += "$name\$($_.Name) : must not be in the sketch folder - $why"
        }
    }

    Get-ChildItem $dir -Directory | ForEach-Object {
        if ($_.Name -notin "src", "data") {
            $problems += "$name\$($_.Name)\ : unexpected subfolder; Arduino only special-cases src\ and data\"
        }
    }

    $n = (Get-ChildItem $dir -File | Where-Object { $sourcey -contains $_.Extension.ToLower() }).Count
    Write-Host "  note: $name\ holds $n source file(s)"
}

Write-Host ""
if ($problems.Count -gt 0) {
    foreach ($p in $problems) { Write-Host "  FAIL: $p" -ForegroundColor Red }
    Write-Host ""
    Write-Host "SKETCH HYGIENE FAILED ($($problems.Count) problem(s))" -ForegroundColor Red
    Write-Host ""
    exit 1
}
Write-Host "SKETCH HYGIENE OK - each sketch folder holds only its own .ino" -ForegroundColor Green
Write-Host ""
exit 0
