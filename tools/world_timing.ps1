# The M92 screen clock drives gameplay independently of the monitor refresh.
# 6,666,666 pixel clocks / (424 clocks * 262 lines) per arcade frame.
function Get-WorldPlaybackArguments {
    [CmdletBinding()]
    param(
        [ValidateRange(0.1, 10.0)]
        [double]$Speed = 1.0
    )

    $speedText = $Speed.ToString('0.########', [Globalization.CultureInfo]::InvariantCulture)
    @(
        '-speed', $speedText, '-throttle', '-noautoframeskip', '-frameskip', '0',
        '-norefreshspeed', '-nosyncrefresh', '-nowaitvsync', '-lowlatency',
        '-sound', 'auto', '-samplerate', '48000'
    )
}
