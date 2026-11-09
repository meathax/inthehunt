param(
    [string]$Rom = (Join-Path $env:USERPROFILE 'Downloads/inthunt.7z'),
    [switch]$Reference,
    [switch]$StageOneCheck,
    [switch]$CaptureEncounter,
    [switch]$Options,
    [switch]$QuickStart,
    [ValidateSet('Pixels','FlatCRT','FlatCRTFast')]
    [string]$DisplayStyle = 'Pixels',
    [ValidateSet('Black','PressureHull','DeepWater','CustomArt','WorldMarquee','ControlPanel','FanBackground')]
    [string]$BorderStyle = 'Black',
    [Alias('BorderBrightness')]
    [ValidateRange(0,100)]
    [int]$Brightness = 100
)
$ErrorActionPreference = 'Stop'
if ($CaptureEncounter) { $StageOneCheck = $true; $Reference = $true }
if ($Options -and $StageOneCheck) { throw 'Open interactive options separately from a stage capture.' }
$root = Split-Path -Parent $PSScriptRoot
$romFile = Get-Item -LiteralPath $Rom
$mameDir = Join-Path $root '.local/mame'
$world = Join-Path $root '.local/world'
if (!(Test-Path "$mameDir/mame.exe") -or (!$Reference -and !(Test-Path "$world/hybrid.lua"))) {
    throw 'Run tools/setup_reference.ps1 first.'
}
if ($romFile.Name -ne 'inthunt.7z') { throw 'Use the World archive named inthunt.7z.' }
if (!$StageOneCheck) {
    . (Join-Path $root 'tools/world_menu.ps1')
    $menuArgs = @{ Root = $root }
    foreach ($name in 'DisplayStyle','BorderStyle','Brightness') {
        if ($PSBoundParameters.ContainsKey($name)) { $menuArgs[$name] = $PSBoundParameters[$name] }
    }
    if (!$Options -and !$QuickStart) {
        $selection = Show-WorldMenu @menuArgs
        if ($selection.Action -eq 'Exit') { return }
        $Options = $selection.Action -eq 'Options'
    } else {
        $selection = Get-WorldMenuSettings @menuArgs
    }
    $DisplayStyle = $selection.DisplayStyle
    $BorderStyle = $selection.BorderStyle
    $Brightness = $selection.Brightness
}
$arguments = @('inthunt', '-rompath', $romFile.DirectoryName, '-skip_gameinfo', '-window', '-joystick', '-keepaspect', '-aspect', 'auto')
if ($StageOneCheck) {
    $script = Join-Path $root 'tools/stage_one.lua'
    if ($CaptureEncounter) {
        $script = Join-Path $world 'capture_stage_one.lua'
        Get-Content "$root/tools/capture_encounter.lua","$root/tools/stage_one.lua" | Set-Content $script -Encoding ascii
    }
    if (!$Reference) {
        $script = Join-Path $world 'stage_one_hybrid.lua'
        Get-Content "$world/hybrid.lua","$root/tools/stage_one.lua" | Set-Content $script -Encoding ascii
    }
    $label = if ($Reference) { 'reference' } else { 'hybrid' }
    $arguments += @('-autoboot_script', $script, '-cfg_directory', (Join-Path $world 'check-cfg'),
        '-video', 'none', '-sound', 'none', '-nothrottle',
        '-seconds_to_run', '110', '-wavwrite', "$world/$label.wav", '-snapshot_directory', "$world/$label")
} else {
    . (Join-Path $root 'tools/world_display.ps1')
    . (Join-Path $root 'tools/world_timing.ps1')
    $arguments += Get-WorldDisplayArguments -Root $root -Style $DisplayStyle
    $borderArgs = @{ Root=$root; BorderStyle=$BorderStyle; BorderBrightness=$Brightness }
    $preset = Get-WorldArtPresets -Root $root | Where-Object Style -eq $BorderStyle
    if ($preset) {
        $borderArgs.BorderStyle = 'CustomArt'
        $borderArgs.ArtworkPath = $preset.Path
    }
    $arguments += Get-WorldBorderArguments @borderArgs
    $arguments += @(Get-WorldPlaybackArguments)
    $arguments += @('-cfg_directory', (Join-Path $mameDir 'cfg'))
    Write-Host '5 inserts a coin. 1 starts Player 1. Tab opens options.'
    Write-Host 'Remap under Input Settings > Input Assignments (this system). Use arrows, Enter and Esc.'
    if ($Options) {
        $script = Join-Path $root 'tools/world_controls.lua'
        if (!$Reference) {
            $script = Join-Path $world 'controls_hybrid.lua'
            Get-Content "$root/tools/world_controls.lua","$world/hybrid.lua" | Set-Content $script -Encoding ascii
        }
        $arguments += @('-autoboot_script', $script)
    } elseif (!$Reference) {
        $arguments += @('-autoboot_script', "$world/hybrid.lua")
    }
}
Push-Location $mameDir
try {
    if ($StageOneCheck) {
        $log = & ./mame.exe @arguments
        $status = $LASTEXITCODE
        $log | Write-Output
        if ($CaptureEncounter -and !($log -match '^encounter_capture=complete$')) {
            throw 'The World run did not capture the full encounter.'
        }
        if ($status -or !($log -match '^stage=0 lives=3 sound_commands=101$') -or
            !($log -match '^fm_writes=36074 fm_hash=e88e82bb rebuilt_inputs=\d+$')) {
            throw 'The World stage-one run diverged from the recorded M92 reference.'
        }
        if (!$Reference -and !($log -match 'rebuilt_inputs=[1-9]\d*$')) {
            throw 'The stage-one run did not execute the rebuilt input code.'
        }
    } else {
        & ./mame.exe @arguments
    }
    if ($LASTEXITCODE) { throw "MAME exited with code $LASTEXITCODE." }
} finally { Pop-Location }
