# Runs the repository-bundled WLINK with all arguments passed through unchanged.
$repoRoot = Split-Path -Parent $PSScriptRoot
$wlink = Join-Path $repoRoot "tools/wlink/wlink.exe"

if (-not (Test-Path -LiteralPath $wlink -PathType Leaf)) {
    Write-Error "Bundled WLINK not found: $wlink"
    exit 1
}

& $wlink @args
exit $LASTEXITCODE
