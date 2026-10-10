# Build script: configure-only smoke test for Vita cross-compile.
# Run from PowerShell:
#   powershell -ExecutionPolicy Bypass -File Build-Vita\build-vita.ps1
# What it does:
#   1. Ensures $env:VITASDK is set (defaults to C:\Users\troia\Desktop\aichatbotsPSVITA\vitasdk).
#   2. Runs cmake configure with vita.toolchain.cmake + vita-preset.cmake.
#   3. Prints first ~150 lines of errors for triage (no full build yet).
param(
  [string]$VitaSdk = "C:\Users\troia\Desktop\aichatbotsPSVITA\vitasdk",
  [string]$SourceDir = (Split-Path -Parent $PSScriptRoot),
  [string]$BuildDir = (Join-Path $PSScriptRoot "build")
)
$ErrorActionPreference = "Continue"
$env:VITASDK = $VitaSdk
$Toolchain = Join-Path $VitaSdk "share\vita.toolchain.cmake"
$Preset = Join-Path $PSScriptRoot "vita-preset.cmake"

Write-Host "== StepMania Vita configure smoke test =="
Write-Host "VITASDK   : $env:VITASDK"
Write-Host "Toolchain : $Toolchain"
Write-Host "Source    : $SourceDir"
Write-Host "Build     : $BuildDir"
if (!(Test-Path $Toolchain)) { Write-Error "Toolchain not found: $Toolchain"; exit 1 }
if (!(Test-Path $Preset)) { Write-Error "Preset not found: $Preset"; exit 1 }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

# NOTE: top-level CMakeLists does not yet branch on VITA; this configure is
# EXPECTED to fail on desktop-only find_package() calls. The log below is the
# triage artifact requested in the porting plan (step 4).
cmake -S $SourceDir -B $BuildDir --toolchain $Toolchain -C $Preset -DVITA=ON 2>&1 | Tee-Object -FilePath (Join-Path $BuildDir "vita-configure.log")
Write-Host ""
Write-Host "== First errors (triage) =="
Select-String -Path (Join-Path $BuildDir "vita-configure.log") -Pattern "Error|error|FAILED|Could not|not found|undefined" | Select-Object -First 40
