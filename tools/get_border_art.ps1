# Download optional game-specific background artwork for local selection.
$ErrorActionPreference = 'Stop'
$directory = Join-Path (Split-Path -Parent $PSScriptRoot) '.local/art-sources'
New-Item -ItemType Directory -Force -Path $directory | Out-Null
$images = @(
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
