[CmdletBinding()]
param([string]$Rom = $env:INTHUNT_IMPORT_ARCHIVE)
$ErrorActionPreference = 'Stop'
try {
    if ([string]::IsNullOrWhiteSpace($Rom)) { throw 'Choose your World inthunt.zip or inthunt.7z archive.' }
    $root = if ($env:INTHUNT_IMPORT_ROOT) { $env:INTHUNT_IMPORT_ROOT } else { Split-Path -Parent $PSScriptRoot }
    . "$PSScriptRoot/world_import.ps1"
    Write-Output 'progress 0.1 Checking and importing World ROM chips'
    Import-WorldNativeRom -Root $root -Rom $Rom -World $env:INTHUNT_IMPORT_DEST
    if (!(Test-WorldNativeRomCache -Root $root -World $env:INTHUNT_IMPORT_DEST)) { throw 'Imported data failed validation.' }
    Write-Output 'progress 1 Ready to play'
    exit 0
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
