param(
    [Parameter(Mandatory=$true)]
    [string]$Image,
    [Parameter(Mandatory=$true)]
    [string]$Address,
    [Parameter(Mandatory=$true)]
    [string]$ExpectedAlias,
    [string]$ExecAddress = "",
    [string]$Python = "",
    [string]$Port = "COM4"
)

$ErrorActionPreference = "Stop"
$localPython = "F:/Python311/python.exe"
if ([string]::IsNullOrWhiteSpace($Python)) {
    $pathPython = Get-Command python.exe -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $localPython) {
        $Python = $localPython
    } elseif ($pathPython) {
        $Python = $pathPython.Source
    } else {
        throw "Python not found. Add python.exe to PATH or pass -Python."
    }
}
$root = Split-Path -Parent $PSScriptRoot
$imagePath = (Resolve-Path (Join-Path $root $Image)).Path
$tag = $ExpectedAlias.Replace("0x", "")
$stdout = Join-Path $root "build/jump_${tag}_dfu.out"
$stderr = Join-Path $root "build/jump_${tag}_dfu.err"
$uartLog = Join-Path $root "build/jump_${tag}_com4.log"

$serial = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$serial.ReadTimeout = 100
$log = ""
$process = $null

try {
    $serial.Open()
    Start-Sleep -Milliseconds 200
    $serial.ReadExisting() | Out-Null

    $arguments = @(
        "scripts/dfu_download.py",
        $imagePath,
        "--addr", $Address,
        "--erase"
    )
    if ($ExecAddress) {
        $arguments += @("--exec", $ExecAddress)
    }

    $process = Start-Process $Python -ArgumentList $arguments `
        -WorkingDirectory $root -WindowStyle Hidden `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru

    $watch = [Diagnostics.Stopwatch]::StartNew()
    while ($watch.ElapsedMilliseconds -lt 8000) {
        $chunk = $serial.ReadExisting()
        if ($chunk) { $log += $chunk }
        Start-Sleep -Milliseconds 100
    }
    $chunk = $serial.ReadExisting()
    if ($chunk) { $log += $chunk }

    if (-not $process.WaitForExit(5000)) {
        Stop-Process -Id $process.Id -Force
        throw "DFU host process did not exit"
    }
    $process.Refresh()
    $exitCode = $process.ExitCode
    if (-not [string]::IsNullOrEmpty([string]$exitCode) -and $exitCode -ne 0) {
        throw "DFU host process failed with exit code $exitCode"
    }
} finally {
    if ($serial.IsOpen) { $serial.Close() }
    [IO.File]::WriteAllText($uartLog, $log)
}

Get-Content $stdout -ErrorAction SilentlyContinue
if (Test-Path $stderr) {
    $errorText = Get-Content $stderr -Raw
    if ($errorText) { Write-Host $errorText }
}
Write-Host $log

$expectedJump = "[BOOT] Jumping to APP @ $Address"
$expectedPass = "[JUMP-APP] PASS alias=$ExpectedAlias"
if (-not $log.Contains($expectedJump)) {
    throw "Missing COM4 jump log: $expectedJump"
}
if (-not $log.Contains($expectedPass)) {
    throw "Missing COM4 APP confirmation: $expectedPass"
}

Write-Host "Jump test PASS: $Address -> $ExpectedAlias" -ForegroundColor Green
