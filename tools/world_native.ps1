function Get-WorldFileHash {
    param([string]$Path, [string]$Algorithm)
    $hash = if ($Algorithm -eq 'SHA1') { [Security.Cryptography.SHA1]::Create() }
            else { [Security.Cryptography.SHA256]::Create() }
    $stream = [IO.File]::OpenRead($Path)
    try { [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $hash.Dispose() }
}
function Get-WorldTar {
    # Windows bsdtar reads zip and 7z; a GNU tar earlier in PATH (MSYS2) cannot.
    $system = Join-Path $env:SystemRoot 'System32/tar.exe'
    if (Test-Path -LiteralPath $system) { $system } else { 'tar.exe' }
}
function Open-WorldNativeImportLock {
    param([string]$World)
    New-Item -Force -ItemType Directory $World -ErrorAction Stop | Out-Null
    $path = Join-Path $World 'native-import.lock'
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ($true) {
        try { return [IO.File]::Open($path, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException] {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Another World import is still running. Try launching again after it finishes.' }
            Start-Sleep -Milliseconds 50
        }
    }
}

function Write-WorldNativeManifest {
    param([string]$World, $Manifest)
    $path = Join-Path $World 'native-import.ok'
    $temporary = Join-Path $World ('manifest-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        $text = $Manifest | ConvertTo-Json -Depth 6
        [IO.File]::WriteAllText($temporary, $text, (New-Object Text.UTF8Encoding $false))
        if ([IO.File]::Exists($path)) { [IO.File]::Replace($temporary, $path, $null) }
        else { [IO.File]::Move($temporary, $path) }
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Get-WorldNativeCacheManifest {
    param([string]$World, [string]$Metadata, $Regions)
    try {
        $manifest = Get-Content -Raw -LiteralPath (Join-Path $World 'native-import.ok') -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
        if ($manifest.version -ne 1 -or $manifest.metadataSha256 -ne (Get-WorldFileHash $Metadata SHA256) -or
            @($manifest.regions).Count -ne @($Regions).Count) { return $null }
        foreach ($region in $Regions) {
            $entries = @($manifest.regions | Where-Object name -eq $region.name)
            if ($entries.Count -ne 1 -or $entries[0].size -ne $region.size) { return $null }
            $path = Join-Path $World ($region.name + '.bin')
            $file = Get-Item -LiteralPath $path -ErrorAction Stop
            if ($file.Length -ne $region.size -or (Get-WorldFileHash $path SHA256) -ne $entries[0].sha256) { return $null }
        }
        return $manifest
    } catch { return $null }
}

function New-WorldNativeCacheManifest {
    param([string]$World, [string]$Metadata, $Regions)
    # Upgrade older caches using the original chip hashes, so cached installs
    # can continue to work after the user moves the archive elsewhere.
    $records = @()
    try {
        foreach ($region in $Regions) {
            $path = Join-Path $World ($region.name + '.bin')
            $image = [IO.File]::ReadAllBytes($path)
            if ($image.Length -ne $region.size) { return $null }
            foreach ($chip in $region.chips) {
                $data = New-Object byte[] $chip.size
                if ($chip.stride -eq 1) { [Buffer]::BlockCopy($image, $chip.offset, $data, 0, $data.Length) }
                else {
                    for ($i = 0; $i -lt $data.Length; ++$i) { $data[$i] = $image[$chip.offset + 2*$i] }
                }
                $sha1 = [Security.Cryptography.SHA1]::Create()
                try { $actual = [BitConverter]::ToString($sha1.ComputeHash($data)).Replace('-', '') }
                finally { $sha1.Dispose() }
                if ($actual -ne $chip.sha1) { return $null }
            }
            $records += [ordered]@{name=$region.name; size=$region.size; sha256=(Get-WorldFileHash $path SHA256)}
        }
        return [ordered]@{version=1; metadataSha256=(Get-WorldFileHash $Metadata SHA256); archiveSha256=''; regions=$records}
    } catch { return $null }
}

function Test-WorldNativeRomCache {
    param([string]$Root)
    $world = Join-Path $Root '.local/world'
    $metadata = Join-Path $Root 'tools/world_regions.json'
    $regions = Get-Content -Raw -LiteralPath $metadata -ErrorAction Stop | ConvertFrom-Json
    $lock = Open-WorldNativeImportLock $world
    try {
        $manifest = Get-WorldNativeCacheManifest $world $metadata $regions
        if (!$manifest) {
            $manifest = New-WorldNativeCacheManifest $world $metadata $regions
            if ($manifest) { Write-WorldNativeManifest $world $manifest }
        }
        return $null -ne $manifest
    } finally { $lock.Dispose() }
}

function Import-WorldNativeRom {
    param([string]$Root, [string]$Rom)
    $archive = Get-Item -LiteralPath $Rom -ErrorAction Stop
    $world = Join-Path $Root '.local/world'
    $metadata = Join-Path $Root 'tools/world_regions.json'
    $regions = Get-Content -Raw -LiteralPath $metadata -ErrorAction Stop | ConvertFrom-Json
    $lock = Open-WorldNativeImportLock $world
    $scratch = $null
    $retainRecovery = $false
    try {
        $archiveHash = Get-WorldFileHash $archive.FullName SHA256
        $cached = Get-WorldNativeCacheManifest $world $metadata $regions
        if ($cached -and $cached.archiveSha256 -eq $archiveHash) { return }
        $scratch = New-Item -ItemType Directory (Join-Path $world ('import-' + [guid]::NewGuid().ToString('N'))) -ErrorAction Stop
        $members = @($regions | ForEach-Object { $_.chips | ForEach-Object file })
        & (Get-WorldTar) -xf $archive.FullName -C $scratch.FullName @members
        if ($LASTEXITCODE) { throw "Cannot extract the required World chips from $($archive.FullName)." }
        $records = @()
        foreach ($region in $regions) {
            $image = New-Object byte[] $region.size
            foreach ($chip in $region.chips) {
                $file = Get-Item -LiteralPath (Join-Path $scratch.FullName $chip.file) -ErrorAction Stop
                if ($file.Length -ne $chip.size -or (Get-WorldFileHash $file.FullName SHA1) -ne $chip.sha1) {
                    throw "World ROM mismatch for $($chip.file)."
                }
                $data = [IO.File]::ReadAllBytes($file.FullName)
                if ($chip.stride -eq 1) { [Buffer]::BlockCopy($data, 0, $image, $chip.offset, $data.Length) }
                else {
                    for ($i = 0; $i -lt $data.Length; ++$i) { $image[$chip.offset + 2*$i] = $data[$i] }
                }
            }
            $path = Join-Path $scratch.FullName ($region.name + '.bin')
            [IO.File]::WriteAllBytes($path, $image)
            $records += [ordered]@{name=$region.name; size=$region.size; sha256=(Get-WorldFileHash $path SHA256)}
        }
        $manifest = [ordered]@{version=1; metadataSha256=(Get-WorldFileHash $metadata SHA256); archiveSha256=$archiveHash; regions=$records}
        $marker = Join-Path $world 'native-import.ok'
        $previousMarker = Join-Path $scratch.FullName 'previous-marker'
        $installed = @()
        try {
            if ([IO.File]::Exists($marker)) { [IO.File]::Move($marker, $previousMarker) }
            foreach ($region in $regions) {
                $name = $region.name + '.bin'
                $target = Join-Path $world $name
                $source = Join-Path $scratch.FullName $name
                $backup = Join-Path $scratch.FullName ($name + '.previous')
                $existed = [IO.File]::Exists($target)
                if ($existed) { [IO.File]::Replace($source, $target, $backup) }
                else { [IO.File]::Move($source, $target) }
                $installed += [pscustomobject]@{target=$target; backup=$backup; existed=$existed}
            }
            Write-WorldNativeManifest $world $manifest
        } catch {
            $installError = $_
            try {
                for ($i = $installed.Count - 1; $i -ge 0; --$i) {
                    $entry = $installed[$i]
                    if ($entry.existed) { [IO.File]::Replace($entry.backup, $entry.target, $null) }
                    else { [IO.File]::Delete($entry.target) }
                }
                if ([IO.File]::Exists($previousMarker)) { [IO.File]::Move($previousMarker, $marker) }
            } catch {
                $retainRecovery = $true
                throw "World import could not restore its previous cache. Recovery files remain in $($scratch.FullName). $($_.Exception.Message)"
            }
            throw $installError
        }
    } finally {
        try {
            if ($scratch -and !$retainRecovery -and
                $scratch.Parent.FullName -eq (Get-Item -LiteralPath $world -ErrorAction Stop).FullName) {
                Remove-Item -Recurse -Force -LiteralPath $scratch.FullName -ErrorAction Stop
            }
        } finally { $lock.Dispose() }
    }
}

function Build-WorldNative {
    param([string]$Root, [string]$Compiler)
    $cache = Join-Path $Root '.local'
    $dest = Join-Path $cache 'native'
    New-Item -Force -ItemType Directory $dest | Out-Null
    $target = Join-Path $dest 'InTheHunt.exe'
    if (!$Compiler) {
        $version = 'zig-x86_64-windows-0.16.0'
        $Compiler = Join-Path $cache "$version/zig.exe"
        if (!(Test-Path -LiteralPath $Compiler)) {
            $archive = Join-Path $cache "$version.zip"
            $hash = '68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e'
            if (!(Test-Path -LiteralPath $archive) -or (Get-WorldFileHash $archive SHA256) -ne $hash) {
                & curl.exe -fL --retry 2 -o "$archive.part" "https://ziglang.org/download/0.16.0/$version.zip"
                if ($LASTEXITCODE -or (Get-WorldFileHash "$archive.part" SHA256) -ne $hash) { throw 'Could not obtain the pinned Zig C compiler.' }
                Move-Item -Force -LiteralPath "$archive.part" -Destination $archive
            }
            & (Get-WorldTar) -xf $archive -C $cache
            if ($LASTEXITCODE) { throw 'Could not unpack Zig.' }
        }
    }
    $names = @('native','clock','cycle_costs','timing_options','settings','input','border','menu','scene','audio_output','ga20','audio_driver','audio_notes','boot','window','video','dispatch','title','title_master','gameover','enemy_target','stage','fire',
        'camera','torpedo','contact','terrain','death','score','candidates','player','frame','frame_video','palette')
    $sources = @($names | ForEach-Object { Join-Path $Root "src/world_$_.c" })
    $temp = Join-Path $dest ('build-' + [guid]::NewGuid().ToString('N') + '.exe')
    $cppSources = @('src/world_ym.cpp', 'src/third_party/ymfm/ymfm_opm.cpp', 'src/world_gpu.cpp', 'src/post/post_chain.cpp')
    $objects = @()
    try {
        foreach ($source in $cppSources) {
            $object = $temp + '.' + $objects.Count + '.o'
            $objects += $object
            $standard = if ($source -match 'gpu|post') { '-std=c++20' } else { '-std=c++17' }
            & $Compiler c++ -target x86_64-windows-gnu $standard -O2 -c (Join-Path $Root $source) -o $object
            if ($LASTEXITCODE) { throw "Native C++ build failed for $source." }
        }
        & $Compiler cc -target x86_64-windows-gnu -std=c99 -O2 -s -Wall -Wextra @sources @objects -o $temp -lgdi32 -luser32 -lkernel32 -lwinmm -lole32 -lwindowscodecs -luuid -ld3d12 -ldxgi -ld3dcompiler_47 -lc++
        if ($LASTEXITCODE) { throw 'Native Windows build failed.' }
        try {
            Move-Item -Force -LiteralPath $temp -Destination $target -ErrorAction Stop
        } catch {
            throw "Could not replace $target. Close the running game and build again. $($_.Exception.Message)"
        }
    } finally {
        foreach ($file in @($temp) + $objects) {
            if (Test-Path -LiteralPath $file) { Remove-Item -Force -LiteralPath $file }
        }
    }
    # CRT shader runtime (librashader, MPL-2.0; DirectX Shader Compiler) beside the game.
    foreach ($dll in 'librashader.dll', 'dxcompiler.dll') {
        $from = Join-Path $Root "third_party/librashader/$dll"
        if (Test-Path -LiteralPath $from) { Copy-Item -Force -LiteralPath $from -Destination (Join-Path $dest $dll) }
    }
    Write-Host "Built $target"
}
