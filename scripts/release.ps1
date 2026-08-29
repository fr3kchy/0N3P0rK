# scripts/make_release.ps1
#
# Builds 0N3P0rK and merges bootloader + partition table + OTA-select +
# app into ONE flashable .bin (offset 0x0), plus a manifest.json for
# ESP Web Tools (https://esphome.github.io/esp-web-tools/) - the standard
# "flash from the browser over USB, no install" widget for ESP32 sites.
# Everything is written to docs/ (next to this script) - upload that
# folder yourself however you like.
#
# Usage (from VS Code's terminal, in the project folder):
#   powershell -ExecutionPolicy Bypass -File scripts\make_release.ps1
#
# Or just right-click the file in VS Code's Explorer -> "Run PowerShell".
# If Windows blocks the very first run with a "scripts disabled" error,
# that -ExecutionPolicy Bypass above is exactly what works around it -
# it only affects this one invocation, nothing system-wide changes.

$ErrorActionPreference = "Stop"

$EnvName      = "m5cardputer"
$ProjectRoot  = Split-Path -Parent $PSScriptRoot
$BuildDir     = Join-Path $ProjectRoot ".pio\build\$EnvName"
$DistDir      = Join-Path $ProjectRoot "docs"

Set-Location $ProjectRoot

# --- version tag for the output filename -----------------------------------
$Version = "0.0.0"
$verLine = Select-String -Path "platformio.ini" -Pattern '^\s*custom_version\s*=\s*(.+?)\s*$' | Select-Object -First 1
if ($verLine) { $Version = $verLine.Matches[0].Groups[1].Value }
$DateTag = (Get-Date).ToUniversalTime().ToString("yyyyMMdd")
$OutName = "0N3P0rK-v$Version-$DateTag"

Write-Host "==> Building $EnvName (version $Version)..."
if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
    Write-Error "'pio' is not recognized on PATH.`nOpen VS Code's PlatformIO CLI terminal instead (PlatformIO ant icon in the sidebar -> PIO Home -> 'Open Terminal', or Ctrl+Shift+P -> 'PlatformIO: New Terminal') and run this script from there - it has pio on PATH automatically."
    exit 1
}
& pio run -e $EnvName
if ($LASTEXITCODE -ne 0) {
    Write-Error "pio run failed (exit code $LASTEXITCODE) - see build log above."
    exit 1
}

$Bootloader = Join-Path $BuildDir "bootloader.bin"
$Partitions = Join-Path $BuildDir "partitions.bin"
$BootApp0   = Join-Path $BuildDir "boot_app0.bin"   # OTA-select data, ships with the Arduino core
$Firmware   = Join-Path $BuildDir "firmware.bin"

foreach ($f in @($Bootloader, $Partitions, $Firmware)) {
    if (-not (Test-Path $f)) {
        Write-Error "Expected build output missing: $f`n(pio run should have produced this - check the build log above)"
        exit 1
    }
}

# boot_app0.bin isn't always copied into .pio\build by every platform
# version - if missing, pull it straight from the Arduino core package
# PlatformIO already downloaded for this project.
if (-not (Test-Path $BootApp0)) {
    $found = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages\framework-arduinoespressif32" `
                            -Filter "boot_app0.bin" -Recurse -ErrorAction SilentlyContinue |
             Select-Object -First 1
    if ($found) {
        $BootApp0 = $found.FullName
    } else {
        Write-Error "boot_app0.bin not found in build output or platform packages.`nRe-run 'pio run -t upload -v' once with the board attached and check its logged command for the real path, or reinstall the espressif32 platform if this is a fresh machine."
        exit 1
    }
}

# --- find esptool: PATH first, then PlatformIO's own bundled copy ----------
function Find-EspTool {
    $onPath = Get-Command esptool.py -ErrorAction SilentlyContinue
    if ($onPath) { return @($onPath.Source) }
    $onPath = Get-Command esptool -ErrorAction SilentlyContinue
    if ($onPath) { return @($onPath.Source) }

    $pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
    $pioEsptool = Get-ChildItem -Path "$env:USERPROFILE\.platformio\packages" `
                                 -Filter "esptool.py" -Recurse -ErrorAction SilentlyContinue |
                  Select-Object -First 1
    if ((Test-Path $pioPython) -and $pioEsptool) {
        return @($pioPython, $pioEsptool.FullName)
    }

    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py) { return @($py.Source, "-m", "esptool") }
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) { return @($py.Source, "-m", "esptool") }

    return $null
}

$EspTool = Find-EspTool
if (-not $EspTool) {
    Write-Error "Could not find esptool.py anywhere (not on PATH, not in .platformio\packages, no python/py on PATH either).`nOpen the PlatformIO CLI terminal (PlatformIO icon -> PIO Home -> Terminal) and try again from there - it has everything on PATH."
    exit 1
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$Merged = Join-Path $DistDir "$OutName.bin"

Write-Host "==> Merging into a single flashable image: $Merged"
# Offsets below match partitions.csv (factory app @ 0x10000) and the
# standard Arduino-ESP32 layout for ESP32-S3 (bootloader @ 0x0, table @
# 0x8000, OTA-select @ 0xe000). --flash_mode/freq/size are "keep" so
# esptool reads the real values baked into bootloader.bin/firmware.bin
# instead of us guessing and silently getting it wrong.
$EspToolExe  = $EspTool[0]
$EspToolArgs = @()
if ($EspTool.Length -gt 1) { $EspToolArgs = $EspTool[1..($EspTool.Length - 1)] }

$mergeArgs = $EspToolArgs + @(
    "--chip", "esp32s3", "merge_bin",
    "-o", $Merged,
    "--flash_mode", "keep",
    "--flash_freq", "keep",
    "--flash_size", "keep",
    "0x0000",  $Bootloader,
    "0x8000",  $Partitions,
    "0xe000",  $BootApp0,
    "0x10000", $Firmware
)
& $EspToolExe @mergeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "esptool merge_bin failed (exit code $LASTEXITCODE) - see output above."
    exit 1
}

Write-Host "==> Writing ESP Web Tools manifest.json"
$manifest = @{
    name                     = "0N3P0rK"
    version                  = $Version
    new_install_prompt_erase = $true
    builds                   = @(
        @{
            chipFamily = "ESP32-S3"
            parts      = @(@{ path = "$OutName.bin"; offset = 0 })
        }
    )
} | ConvertTo-Json -Depth 5
Set-Content -Path (Join-Path $DistDir "manifest.json") -Value $manifest -Encoding utf8

Copy-Item -Path $Merged -Destination (Join-Path $DistDir "latest.bin") -Force

Write-Host ""
Write-Host "Done. Everything is in docs\ - upload that folder to your site yourself:"
Write-Host "  $DistDir\$OutName.bin"
Write-Host "  $DistDir\manifest.json"
Write-Host "  $DistDir\latest.bin        (stable filename, always the newest build)"
