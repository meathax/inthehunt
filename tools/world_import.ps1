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
    param([string]$Root, [string]$World)
    if (!$World) { $World = Join-Path $Root '.local/world' }
    $metadata = Join-Path $PSScriptRoot 'world_regions.json'
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
    param([string]$Root, [string]$Rom, [string]$World)
    $archive = Get-Item -LiteralPath $Rom -ErrorAction Stop
    if (!$World) { $World = Join-Path $Root '.local/world' }
    $metadata = Join-Path $PSScriptRoot 'world_regions.json'
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
        # Windows tar can narrow non-ASCII archive paths. Stage the archive with
        # an ASCII name and use a relative path from the inherited directory.
        Copy-Item -LiteralPath $archive.FullName -Destination (Join-Path $scratch.FullName 'source.archive')
        Push-Location -LiteralPath $scratch.FullName
        try {
            & (Get-WorldTar) -xf 'source.archive' @members
            if ($LASTEXITCODE) { throw "Cannot extract the required World chips from $($archive.FullName)." }
        } finally { Pop-Location }
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
