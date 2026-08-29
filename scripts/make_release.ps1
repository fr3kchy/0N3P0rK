```powershell
# scripts/make_release.ps1
#
# Builds TWO flashable images for 0N3P0rK:
#
# 1) M5Launcher image:
#    bootloader + partitions + firmware
#
# 2) Full image:
#    bootloader + partitions + boot_app0 + firmware
#
# Both images start at offset 0x0.
# Output is written to docs\.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\make_release.ps1

$ErrorActionPreference = "Stop"

$EnvName     = "m5cardputer"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir    = Join-Path $ProjectRoot ".pio\build\$EnvName"
$DistDir     = Join-Path $ProjectRoot "docs"

Set-Location $ProjectRoot


# ---------------------------------------------------------------------------
# VERSION
# ---------------------------------------------------------------------------

$Version = "0.0.0"

$verLine = Select-String `
    -Path "platformio.ini" `
    -Pattern '^\s*custom_version\s*=\s*(.+?)\s*$' |
    Select-Object -First 1

if ($verLine) {
    $Version = $verLine.Matches[0].Groups[1].Value.Trim()
}

$DateTag = (Get-Date).ToUniversalTime().ToString("yyyyMMdd")

$BaseName = "0N3P0rK-v$Version-$DateTag"

Write-Host ""
Write-Host "============================================"
Write-Host "  0N3P0rK RELEASE BUILDER"
Write-Host "============================================"
Write-Host ""
Write-Host "Environment : $EnvName"
Write-Host "Version     : $Version"
Write-Host "Date        : $DateTag"
Write-Host ""


# ---------------------------------------------------------------------------
# CHECK PLATFORMIO
# ---------------------------------------------------------------------------

Write-Host "==> Checking PlatformIO..."

if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
    Write-Error @"
'pio' is not recognized on PATH.

Open the PlatformIO CLI terminal and run this script there.
"@
    exit 1
}


# ---------------------------------------------------------------------------
# BUILD
# ---------------------------------------------------------------------------

Write-Host "==> Building $EnvName..."

& pio run -e $EnvName

if ($LASTEXITCODE -ne 0) {
    Write-Error "pio run failed (exit code $LASTEXITCODE)."
    exit 1
}


# ---------------------------------------------------------------------------
# BUILD FILES
# ---------------------------------------------------------------------------

$Bootloader = Join-Path $BuildDir "bootloader.bin"
$Partitions = Join-Path $BuildDir "partitions.bin"
$BootApp0   = Join-Path $BuildDir "boot_app0.bin"
$Firmware   = Join-Path $BuildDir "firmware.bin"

foreach ($f in @(
    $Bootloader,
    $Partitions,
    $Firmware
)) {
    if (-not (Test-Path $f)) {
        Write-Error "Required build output missing: $f"
        exit 1
    }
}


# ---------------------------------------------------------------------------
# FIND boot_app0.bin
# ---------------------------------------------------------------------------

if (-not (Test-Path $BootApp0)) {

    Write-Host "==> boot_app0.bin not found in build directory."
    Write-Host "    Searching PlatformIO Arduino framework..."

    $FrameworkDir = Join-Path `
        $env:USERPROFILE `
        ".platformio\packages\framework-arduinoespressif32"

    $found = Get-ChildItem `
        -Path $FrameworkDir `
        -Filter "boot_app0.bin" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($found) {
        $BootApp0 = $found.FullName
    }
    else {
        Write-Error @"
boot_app0.bin was not found.

Expected either:
$BuildDir\boot_app0.bin

or inside:
$FrameworkDir
"@
        exit 1
    }
}


# ---------------------------------------------------------------------------
# FIND ESPTOOL
# ---------------------------------------------------------------------------

function Find-EspTool {

    $onPath = Get-Command esptool.py -ErrorAction SilentlyContinue

    if ($onPath) {
        return @($onPath.Source)
    }

    $onPath = Get-Command esptool -ErrorAction SilentlyContinue

    if ($onPath) {
        return @($onPath.Source)
    }

    $pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

    $pioEsptool = Get-ChildItem `
        -Path "$env:USERPROFILE\.platformio\packages" `
        -Filter "esptool.py" `
        -Recurse `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ((Test-Path $pioPython) -and $pioEsptool) {
        return @(
            $pioPython,
            $pioEsptool.FullName
        )
    }

    $py = Get-Command python -ErrorAction SilentlyContinue

    if ($py) {
        return @(
            $py.Source,
            "-m",
            "esptool"
        )
    }

    $py = Get-Command py -ErrorAction SilentlyContinue

    if ($py) {
        return @(
            $py.Source,
            "-m",
            "esptool"
        )
    }

    return $null
}


$EspTool = Find-EspTool

if (-not $EspTool) {
    Write-Error @"
Could not find esptool.

Open the PlatformIO terminal and try again.
"@
    exit 1
}

$EspToolExe  = $EspTool[0]
$EspToolArgs = @()

if ($EspTool.Length -gt 1) {
    $EspToolArgs = $EspTool[1..($EspTool.Length - 1)]
}


# ---------------------------------------------------------------------------
# OUTPUT DIRECTORY
# ---------------------------------------------------------------------------

New-Item `
    -ItemType Directory `
    -Force `
    -Path $DistDir |
    Out-Null


# ---------------------------------------------------------------------------
# OUTPUT FILENAMES
# ---------------------------------------------------------------------------

$M5LauncherBin = Join-Path `
    $DistDir `
    "$BaseName-M5Launcher.bin"

$FullBin = Join-Path `
    $DistDir `
    "$BaseName-Full.bin"


# ---------------------------------------------------------------------------
# IMAGE 1 — M5LAUNCHER
#
# 3 COMPONENTS:
#
# 0x0000  bootloader
# 0x8000  partitions
# 0x10000 firmware
#
# boot_app0 intentionally NOT included.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "============================================"
Write-Host "  BUILDING M5LAUNCHER IMAGE"
Write-Host "============================================"
Write-Host ""

$mergeArgsM5 = $EspToolArgs + @(
    "--chip", "esp32s3",
    "merge_bin",
    "-o", $M5LauncherBin,

    "--flash_mode", "keep",
    "--flash_freq", "keep",
    "--flash_size", "keep",

    "0x0000",  $Bootloader,
    "0x8000",  $Partitions,
    "0x10000", $Firmware
)

& $EspToolExe @mergeArgsM5

if ($LASTEXITCODE -ne 0) {
    Write-Error "M5Launcher merge failed (exit code $LASTEXITCODE)."
    exit 1
}


# ---------------------------------------------------------------------------
# IMAGE 2 — FULL
#
# 4 COMPONENTS:
#
# 0x0000  bootloader
# 0x8000  partitions
# 0xE000  boot_app0
# 0x10000 firmware
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "============================================"
Write-Host "  BUILDING FULL IMAGE"
Write-Host "============================================"
Write-Host ""

$mergeArgsFull = $EspToolArgs + @(
    "--chip", "esp32s3",
    "merge_bin",
    "-o", $FullBin,

    "--flash_mode", "keep",
    "--flash_freq", "keep",
    "--flash_size", "keep",

    "0x0000",  $Bootloader,
    "0x8000",  $Partitions,
    "0xE000",  $BootApp0,
    "0x10000", $Firmware
)

& $EspToolExe @mergeArgsFull

if ($LASTEXITCODE -ne 0 {
    Write-Error "Full image merge failed (exit code $LASTEXITCODE)."
    exit 1
}


# ---------------------------------------------------------------------------
# MANIFEST FOR ESP WEB TOOLS
#
# Uses the FULL 4-component image.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "==> Writing ESP Web Tools manifest.json"

$manifest = @{
    name = "0N3P0rK"
    version = $Version
    new_install_prompt_erase = $true
    builds = @(
        @{
            chipFamily = "ESP32-S3"
            parts = @(
                @{
                    path = "$BaseName-Full.bin"
                    offset = 0
                }
            )
        }
    )
} | ConvertTo-Json -Depth 5

Set-Content `
    -Path (Join-Path $DistDir "manifest.json") `
    -Value $manifest `
    -Encoding utf8


# ---------------------------------------------------------------------------
# LATEST FULL IMAGE
# ---------------------------------------------------------------------------

Copy-Item `
    -Path $FullBin `
    -Destination (Join-Path $DistDir "latest.bin") `
    -Force


# ---------------------------------------------------------------------------
# DONE
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "============================================"
Write-Host "  BUILD COMPLETE"
Write-Host "============================================"
Write-Host ""

Write-Host "M5Launcher (3 parts):"
Write-Host "  $M5LauncherBin"
Write-Host ""

Write-Host "Full image (4 parts):"
Write-Host "  $FullBin"
Write-Host ""

Write-Host "ESP Web Tools:"
Write-Host "  $(Join-Path $DistDir "manifest.json")"
Write-Host ""

Write-Host "Latest full image:"
Write-Host "  $(Join-Path $DistDir "latest.bin")"
Write-Host ""
```
