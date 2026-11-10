param([string]$Root, [string[]]$Modes = @('steer', 'steer z x'), [int]$Frames = 9000, [int]$PlayFrom = 2040,
      [int]$ShotEvery = 900, [switch]$NoBuild)
# Hidden, silent, unpaced scripted runs of the native game. Prints one line per mode.
$ErrorActionPreference = 'Stop'
. "$Root/tools/world_native.ps1"
if (!$NoBuild) {
    try { Build-WorldNative -Root $Root *>&1 | Where-Object { "$_" -match 'world_\w+\.[ch]:\d+:\d+: error|Built' } | Select-Object -First 12 }
    catch { "BUILD FAILED: $($_.Exception.Message)"; return }
}
Add-Type -AssemblyName System.Drawing
$world = Join-Path $Root '.local/world'
$out = Join-Path $world 'headless'
New-Item -Force -ItemType Directory $out | Out-Null
$dirs = 0x01, 0x04, 0x01, 0x08, 0x02, 0x01      # right down right up left right
foreach ($mode in $Modes) {
    $tag = $mode -replace ' ', ''
    $lines = New-Object System.Collections.Generic.List[string]
    $prev = ''
    for ($f = 0; $f -lt $Frames; $f++) {
        $held = 0; $buttons = 0
        if ($f -ge 300 -and $f -lt 306) { $buttons = 4 }
        if (($f -ge 420 -and $f -lt 426) -or ($f -ge 600 -and $f -lt 606)) { $buttons = 1 }
        if ($f -ge $PlayFrom) {
            if ($mode -match 'steer') { $held = $held -bor $dirs[[int][math]::Floor(($f - $PlayFrom) / 90) % $dirs.Count] }
            $phase = ($f - $PlayFrom) % 24
            if ($mode -match 'z' -and $phase -lt 6) { $held = $held -bor 0x80 }
            if ($mode -match 'x' -and $phase -ge 12 -and $phase -lt 18) { $held = $held -bor 0x40 }
        }
        $shot = ($ShotEvery -gt 0 -and $f -gt 0 -and $f % $ShotEvery -eq 0)
        $state = '{0:x} {1:x}' -f $held, $buttons
        if ($state -ne $prev -or $shot) { $lines.Add(('{0} {1}{2}' -f $f, $state, $(if ($shot) { ' shot' } else { '' }))); $prev = $state }
    }
    $lines.Add("$Frames end")
    $script = Join-Path $out "$tag.txt"
    [IO.File]::WriteAllLines($script, $lines)
    Remove-Item (Join-Path $world 'native-stops.tsv'), (Join-Path $world 'headless-*.bmp') -ErrorAction SilentlyContinue
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath (Join-Path $Root '.local/native/InTheHunt.exe') -ArgumentList "`"$world`"", '--timing=arcade', "`"--headless=$script`"" `
        -PassThru -WindowStyle Hidden -RedirectStandardError (Join-Path $out "$tag.err.txt")
    if (!$p.WaitForExit(240000)) { Stop-Process -Id $p.Id -Force; $result = 'TIMEOUT' } else { $result = "exit $($p.ExitCode)" }
    $stop = if (Test-Path (Join-Path $world 'native-stops.tsv')) { Get-Content (Join-Path $world 'native-stops.tsv') -First 1 } else { "no stop in $Frames frames" }
    Get-ChildItem $world -Filter 'headless-*.bmp' | ForEach-Object {
        $bmp = [Drawing.Bitmap]::FromFile($_.FullName)
        $bmp.Save((Join-Path $out ("$tag-" + $_.BaseName.Substring(9) + '.png')), [Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose(); Remove-Item $_.FullName
    }
    "$mode :: $result in $([int]$sw.Elapsed.TotalSeconds)s :: $stop"
}
