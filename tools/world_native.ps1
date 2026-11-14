. "$PSScriptRoot/world_import.ps1"

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
    $names = @('native','setup','clock','cycle_costs','timing_options','settings','input','border','menu','scene','audio_output','ga20','audio_driver','audio_notes','boot','window','video','dispatch','title','title_master','gameover','enemy_target','stage','fire',
        'camera','torpedo','contact','terrain','death','score','candidates','player','frame','frame_video','palette')
    $sources = @($names | ForEach-Object { Join-Path $Root "src/world_$_.c" })
    $temp = Join-Path $dest ('build-' + [guid]::NewGuid().ToString('N') + '.exe')
    $cppSources = @('src/world_ym.cpp', 'src/third_party/ymfm/ymfm_opm.cpp', 'src/world_gpu.cpp', 'src/post/post_chain.cpp')
    $objects = @()
    $resource = $temp + ".res"
    try {
        & $Compiler rc /fo $resource (Join-Path $Root "src/inthehunt.rc")
        if ($LASTEXITCODE) { throw "Icon resource compilation failed." }
        foreach ($source in $cppSources) {
            $object = $temp + '.' + $objects.Count + '.o'
            $objects += $object
            $standard = if ($source -match 'gpu|post') { '-std=c++20' } else { '-std=c++17' }
            & $Compiler c++ -target x86_64-windows-gnu $standard -O2 -c (Join-Path $Root $source) -o $object
            if ($LASTEXITCODE) { throw "Native C++ build failed for $source." }
        }
        & $Compiler cc -target x86_64-windows-gnu -std=c99 -O2 -s -Wall -Wextra -municode @sources @objects $resource -o $temp -lcomdlg32 -lgdi32 -luser32 -lkernel32 -lwinmm -lole32 -lwindowscodecs -luuid -ld3d12 -ldxgi -ld3dcompiler_47 -lc++
        if ($LASTEXITCODE) { throw 'Native Windows build failed.' }
        try {
            Move-Item -Force -LiteralPath $temp -Destination $target -ErrorAction Stop
        } catch {
            throw "Could not replace $target. Close the running game and build again. $($_.Exception.Message)"
        }
    } finally {
        foreach ($file in @($temp, $resource) + $objects) {
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
