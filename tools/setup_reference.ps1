param(
    [string]$Rom = (Join-Path $env:USERPROFILE 'Downloads/inthunt.7z'),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$romFile = Get-Item -LiteralPath $Rom
$cache = Join-Path $root '.local'
New-Item -ItemType Directory -Force $cache | Out-Null

function Install-Tool($folder, $package, $url, $hash, $members) {
    $archive = Join-Path $cache $package
    if (!(Test-Path $archive) -or (Get-FileHash $archive).Hash -ne $hash) {
        & curl.exe -fL --retry 2 -o "$archive.part" $url
        if ($LASTEXITCODE -or (Get-FileHash "$archive.part").Hash -ne $hash) {
            throw "Could not obtain the pinned $package."
        }
        Move-Item -Force "$archive.part" $archive
    }
    $dest = Join-Path $cache $folder
    New-Item -ItemType Directory -Force $dest | Out-Null
    & tar.exe -xf $archive -C $dest @members
    if ($LASTEXITCODE) { throw "Could not unpack $package." }
}

if (!(Test-Path "$cache/mame/mame.exe")) {
    Install-Tool 'mame' 'mame.exe' 'https://github.com/mamedev/mame/releases/download/mame0289/mame0289b_x64.exe' 'A1AA7912168C9D1B05E611906BC21B8B9BE3935822AEAD36D12A1DA363150B7D' @()
}
if (!(Test-Path "$cache/watcom/binnt64/wcc.exe")) {
    Install-Tool 'watcom' 'watcom-pkg.exe' 'https://github.com/open-watcom/open-watcom-v2/releases/download/2026-11-01-Build/open-watcom-2_0-c-win-x64.exe' 'D4E496C593EF2ACBC54D1EAEB31B31D9C62F86E39A09E61ADA411EF34FEAA01A' @('binnt64')
}
$revision = '73346bee98cd79d605514d14691f40ee5e368915'
$metadata = Join-Path $cache 'mame-metadata'
New-Item -ItemType Directory -Force $metadata | Out-Null
foreach ($name in 'm92.cpp','irem_cpu.cpp') {
    $target = Join-Path $metadata $name
    if (!(Test-Path $target)) {
        & curl.exe -fsSL --retry 2 -o "$target.part" "https://raw.githubusercontent.com/mamedev/mame/$revision/src/mame/irem/$name"
        if ($LASTEXITCODE) { throw "Failed to download $name" }
        Move-Item -Force "$target.part" $target
    }
}
& $Python "$root/tools/import_world.py" $romFile.FullName
if ($LASTEXITCODE) { throw 'World ROM import failed.' }
& $Python "$root/tools/build_world.py"
if ($LASTEXITCODE) { throw 'World build failed.' }
