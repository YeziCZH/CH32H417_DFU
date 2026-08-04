# flash.ps1 — CH32H417 DFU Bootloader flash script
# Reliable flow: pin-rst erase → low speed flash (no -e)
param(
    [string]$Firmware = "build/ch32h417_dfu.bin",
    [string]$Wlink    = "",
    [string]$Chip     = "CH32H41X",
    [int]   $Device   = 0,
    [string]$Address  = "0x08000000",
    [string]$Speed    = "medium"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

function Resolve-ProjectPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$Firmware = Resolve-ProjectPath $Firmware

if ([string]::IsNullOrWhiteSpace($Wlink)) {
    $localWlink = Join-Path $repoRoot "tools/wlink/wlink.exe"
    $pathWlink = Get-Command wlink.exe -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $localWlink) {
        $Wlink = $localWlink
    } elseif ($pathWlink) {
        $Wlink = $pathWlink.Source
    } else {
        Write-Host "ERROR: wlink.exe not found in tools/wlink, PATH, or -Wlink." -ForegroundColor Red
        exit 1
    }
} elseif ([System.IO.Path]::IsPathRooted($Wlink)) {
    $Wlink = [System.IO.Path]::GetFullPath($Wlink)
} else {
    $repoWlink = Resolve-ProjectPath $Wlink
    $pathWlink = Get-Command $Wlink -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $repoWlink -PathType Leaf) {
        $Wlink = $repoWlink
    } elseif ($pathWlink) {
        $Wlink = $pathWlink.Source
    }
}

if (-not (Test-Path -LiteralPath $Wlink -PathType Leaf)) {
    Write-Host "ERROR: wlink.exe not found: $Wlink" -ForegroundColor Red
    exit 1
}

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  CH32H417 DFU Bootloader Flash" -ForegroundColor Cyan
Write-Host "  Chip: $Chip  Dev: $Device  Speed: $Speed" -ForegroundColor Cyan
Write-Host "  Image: $Firmware" -ForegroundColor Cyan
Write-Host "  WLINK: $Wlink" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

if (-not (Test-Path -LiteralPath $Firmware -PathType Leaf)) {
    Write-Host "ERROR: Firmware not found: $Firmware" -ForegroundColor Red
    Write-Host "Run the CMake Build task first." -ForegroundColor Yellow
    exit 1
}

$size = (Get-Item -LiteralPath $Firmware).Length
Write-Host "Firmware: $size bytes" -ForegroundColor Gray

# ── Step 1: Pin-RST erase ─────────────────────────────────────────────
Write-Host "`n[1/2] Reset and erase via pin-rst..." -ForegroundColor Yellow
& $Wlink -d $Device --chip $Chip erase --method pin-rst
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Erase failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "Erase done." -ForegroundColor Green

# ── Step 2: Flash (no -e, already erased) ──────────────────────────────
Write-Host "`n[2/2] Flashing @ $Address ..." -ForegroundColor Yellow
& $Wlink -d $Device --chip $Chip --speed $Speed flash --address $Address $Firmware
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Flash failed!" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "`n========================================" -ForegroundColor Green
Write-Host "  Flash complete!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
