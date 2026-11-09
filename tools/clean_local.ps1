# Remove regenerable build debris from the private .local scratch folder.
# Nothing under .local is in source control, so this never touches the project
# itself. Imported ROM images, settings, captures, notes, probe scripts and the
# installed toolchains are kept.
#
#   .\tools\clean_local.ps1 -WhatIf      preview only
#   .\tools\clean_local.ps1              delete build products
#   .\tools\clean_local.ps1 -Downloads   also delete the cached tool archives
#                                        (fetched again on demand)
[CmdletBinding(SupportsShouldProcess)]
param([switch]$Downloads)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$local = Join-Path $root '.local'
if (!(Test-Path -LiteralPath $local -PathType Container)) { Write-Host 'Nothing to clean.'; return }
$archives = @('mame.exe', 'watcom-pkg.exe', 'zig-x86_64-windows-0.16.0.zip')
$targets = @()
# Compiler output left beside the probe sources at the top level only.
$targets += @(Get-ChildItem -LiteralPath $local -File | Where-Object {
    $_.Extension -in '.exe', '.pdb', '.o', '.obj', '.map' -and $_.Name -notin $archives
})
# Debug symbols from interrupted builds, beside the current InTheHunt.exe.
$native = Join-Path $local 'native'
if (Test-Path -LiteralPath $native) {
    $targets += @(Get-ChildItem -LiteralPath $native -File -Filter 'build-*')
}
# Unpacked copy made by the release smoke test, test binaries, Python caches.
foreach ($folder in (Join-Path $local 'native-release-check'), (Join-Path $local 'tests'),
                    (Join-Path $root 'tools/__pycache__')) {
    if (Test-Path -LiteralPath $folder -PathType Container) { $targets += Get-Item -LiteralPath $folder }
}
if ($Downloads) {
    foreach ($name in $archives) {
        $path = Join-Path $local $name
        if (Test-Path -LiteralPath $path -PathType Leaf) { $targets += Get-Item -LiteralPath $path }
    }
}
$freed = 0
foreach ($item in $targets) {
    $size = if ($item.PSIsContainer) {
        (Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Force | Measure-Object Length -Sum).Sum
    } else { $item.Length }
    if ($PSCmdlet.ShouldProcess($item.FullName, 'Delete')) {
        Remove-Item -LiteralPath $item.FullName -Recurse -Force
        $freed += [long]$size
    }
}
Write-Host ('{0} items removed, {1:N1} MB freed.' -f $targets.Count, ($freed / 1MB))
