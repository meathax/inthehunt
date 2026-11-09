# Download optional game-specific background artwork for local selection.
$ErrorActionPreference = 'Stop'
$directory = Join-Path (Split-Path -Parent $PSScriptRoot) '.local/art-sources'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$images = @(
    @('world-marquee.jpg','https://images.launchbox-app.com/6b33487a-8137-4de4-97a5-9fde3f608ef6.jpg','91BD0751159329F45D69635658CDBD49530BD77142F1ED08EC6C9E09186E107D'),
    @('inthunt-cpo.jpg','https://forums.libretro.com/uploads/default/original/3X/3/d/3d042686fd48982d67c1c3c56e494bf0e198fe80.jpeg','3E2B499976EAC7A3B6287040ECD51492CB2307F9467F4FD7AABDE98FD427BFBB'),
    @('world-fan-background.jpg','https://images.launchbox-app.com/258c827f-92ca-459c-8e2a-01bf71b609c1.jpg','38055084116BE487490E9E26EB8E16331DE252CE41D54C26CB153106F1E1F4D2')
)
foreach ($image in $images) {
    $target = Join-Path $directory $image[0]
    if ((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target).Hash -eq $image[2]) { continue }
    $temporary = "$target.$PID.part"
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $image[1] -OutFile $temporary
        if ((Get-FileHash -LiteralPath $temporary).Hash -ne $image[2]) { throw "Source image changed: $($image[0])" }
        Move-Item -LiteralPath $temporary -Destination $target -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
    }
}
Write-Output $directory
