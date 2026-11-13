[CmdletBinding()]
param(
    [string]$Rom = (Join-Path $env:USERPROFILE 'Downloads/inthunt.7z'),
    [switch]$QuickStart,
    [ValidateSet('Arcade','RemoveSlowdown')]
    [string]$TimingMode
)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/tools/world_native.ps1"
$world = Join-Path $PSScriptRoot '.local/world'
$exe = Join-Path $PSScriptRoot '.local/native/InTheHunt.exe'
$needsImport = !(Test-WorldNativeRomCache -Root $PSScriptRoot)
if ($needsImport -or $PSBoundParameters.ContainsKey('Rom')) {
    if (!$PSBoundParameters.ContainsKey('Rom') -and !(Test-Path -LiteralPath $Rom)) {
        $besideLauncher = Join-Path $PSScriptRoot 'inthunt.7z'
        if (Test-Path -LiteralPath $besideLauncher) { $Rom = $besideLauncher }
        else {
            Add-Type -AssemblyName System.Windows.Forms
            $picker = New-Object Windows.Forms.OpenFileDialog
            try {
                $picker.Title = 'Select your In The Hunt World ROM archive'
                $picker.Filter = 'ROM archives (*.7z;*.zip)|*.7z;*.zip|All files|*.*'
                if ($picker.ShowDialog() -ne 'OK') { return }
                $Rom = $picker.FileName
            } finally { $picker.Dispose() }
        }
    }
    Import-WorldNativeRom -Root $PSScriptRoot -Rom $Rom
}
$portable = Join-Path $PSScriptRoot 'InTheHunt.exe'
if (Test-Path -LiteralPath $portable) { $exe = $portable }
else {
    $build = Get-Item -LiteralPath $exe -ErrorAction SilentlyContinue
    $newest = @(
        Get-ChildItem -Recurse -File -Path "$PSScriptRoot/src" -Include *.c,*.h,*.cpp,*.ipp,*.rc
        Get-Item -LiteralPath "$PSScriptRoot/artwork/icon/inthehunt.ico"
        Get-Item -LiteralPath "$PSScriptRoot/tools/world_native.ps1"
    ) |
        Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1
    if (!$build -or $newest.LastWriteTimeUtc -gt $build.LastWriteTimeUtc) { Build-WorldNative -Root $PSScriptRoot }
}
$artScript = Join-Path $PSScriptRoot 'tools/get_border_art.ps1'
if ((Test-Path -LiteralPath $artScript) -and !(Test-Path -LiteralPath (Join-Path $PSScriptRoot '.local/art-sources/world-fan-background.jpg'))) {
    try { & $artScript | Out-Null } catch { Write-Warning "Border art download skipped: $($_.Exception.Message)" }
}
Write-Host '5 inserts a coin. 1 starts Player 1. Arrow keys move. Z and X fire. F1 (or L3+R3) opens the menu.'
$gameArguments = @($world)
if ($PSBoundParameters.ContainsKey('TimingMode')) {
    $modeName = if ($TimingMode -eq 'RemoveSlowdown') { 'remove-slowdown' } else { 'arcade' }
    $gameArguments += "--timing=$modeName"
}
& $exe @gameArguments
if ($LASTEXITCODE) { throw "In The Hunt exited with code $LASTEXITCODE." }
