# Run the portable unit tests with the pinned Zig compiler.
# Usage: .\tests\run_tests.ps1 [-Compiler path\to\zig.exe]
[CmdletBinding()]
param([string]$Compiler)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$Compiler) { $Compiler = Join-Path $root '.local/zig-x86_64-windows-0.16.0/zig.exe' }
if (!(Test-Path -LiteralPath $Compiler)) {
    throw 'Zig was not found. Run .\setup.ps1 or .\play.ps1 once to install it, or pass -Compiler.'
}
$out = Join-Path $root '.local/tests'
New-Item -Force -ItemType Directory $out | Out-Null
# Test program -> the src/world_*.c modules it links.
$tests = [ordered]@{
    'achievements-test'   = @('achievements')
    'clock-test'          = @('clock', 'timing_options')
    'controls-remap-test' = @('input')
    'cycle-cost-test'     = @('cycle_costs')
    'prefetch-test'       = @('cycle_costs')
    'timing-option-test'  = @('timing_options')
}
$failed = @()
# cycle-cost-test reads .local/world/maincpu.bin relative to the project root.
Push-Location $root
try {
    foreach ($name in $tests.Keys) {
        $exe = Join-Path $out "$name.exe"
        $sources = @(Join-Path $PSScriptRoot "$name.c")
        $sources += @($tests[$name] | ForEach-Object { Join-Path $root "src/world_$_.c" })
        & $Compiler cc -target x86_64-windows-gnu -std=c99 -O2 -Wall -Wextra @sources -o $exe
        if ($LASTEXITCODE) { $failed += "$name (build)"; continue }
        & $exe $out
        if ($LASTEXITCODE) { $failed += "$name (exit $LASTEXITCODE)" }
    }
} finally { Pop-Location }
if ($failed) { throw "Failed: $($failed -join ', ')" }
Write-Host "All $($tests.Count) test programs passed."
