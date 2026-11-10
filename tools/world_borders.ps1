function Test-WorldBorderArt {
    param([string]$Root = (Split-Path -Parent $PSScriptRoot))
    Test-Path -LiteralPath (Join-Path $Root '.local/world/artwork/worldcustom/imported.png') -PathType Leaf
}

function Install-WorldBorderFile {
    param([string]$Temporary, [string]$Destination)
    if ([IO.File]::Exists($Destination)) {
        [IO.File]::Replace($Temporary, $Destination, [System.Management.Automation.Language.NullString]::Value)
    } else {
        [IO.File]::Move($Temporary, $Destination)
    }
}

function Import-WorldBorderArt {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$Path, [string]$Root = (Split-Path -Parent $PSScriptRoot))
    ConvertTo-WorldBorderArt -Path $Path -Folder (Join-Path $Root '.local/world/artwork/worldcustom')
}

function ConvertTo-WorldBorderArt {
    param([string]$Path, [string]$Folder)
    $source = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($source.Extension -notin '.jpg','.jpeg','.png') { throw 'Choose a JPEG or PNG image.' }
    Add-Type -AssemblyName System.Drawing
    [void][IO.Directory]::CreateDirectory($folder)
    $temporary = Join-Path $folder ([guid]::NewGuid().ToString() + '.tmp')
    $image = $null
    $canvas = $null
    $graphics = $null
    try {
        $image = [Drawing.Image]::FromFile($source.FullName)
        if ($image.RawFormat.Guid -notin [Drawing.Imaging.ImageFormat]::Jpeg.Guid,[Drawing.Imaging.ImageFormat]::Png.Guid) {
            throw 'Choose a JPEG or PNG image.'
        }
        if ($image.PropertyIdList -contains 274) {
            $orientation = [BitConverter]::ToUInt16($image.GetPropertyItem(274).Value, 0)
            $rotation = @(0,0,4,2,6,5,1,7,3)
            if ($orientation -ge 2 -and $orientation -le 8) {
                $image.RotateFlip([Drawing.RotateFlipType]$rotation[$orientation])
            }
        }
        # Wide pictures form a single panorama. Narrow pictures fill a side panel.
        $wide = $image.Width -ge 2 * $image.Height
        $aspect = if ($wide) { 8 / 3 } else { 2 / 3 }
        $cropHeight = [Math]::Min($image.Height, $image.Width / $aspect)
        $height = [Math]::Max(1, [Math]::Floor([Math]::Min(2160, $cropHeight)))
        $width = [Math]::Max(1, [Math]::Round($height * $aspect))
        $canvas = [Drawing.Bitmap]::new($width, $height)
        $graphics = [Drawing.Graphics]::FromImage($canvas)
        $graphics.Clear([Drawing.Color]::Black)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $scale = [Math]::Max($width / $image.Width, $height / $image.Height)
        $cropWidth = $width / $scale
        $cropHeight = $height / $scale
        $crop = [Drawing.RectangleF]::new(($image.Width-$cropWidth)/2, ($image.Height-$cropHeight)/2, $cropWidth, $cropHeight)
        $graphics.DrawImage($image, [Drawing.RectangleF]::new(0,0,$width,$height), $crop, [Drawing.GraphicsUnit]::Pixel)
        $canvas.Save($temporary, [Drawing.Imaging.ImageFormat]::Png)
        Install-WorldBorderFile $temporary (Join-Path $folder 'imported.png')
    } finally {
        if ($null -ne $graphics) { $graphics.Dispose() }
        if ($null -ne $canvas) { $canvas.Dispose() }
        if ($null -ne $image) { $image.Dispose() }
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
}

function Get-WorldBorderArguments {
    [CmdletBinding()]
    param([ValidateSet('Black','PressureHull','DeepWater','CustomArt')][string]$BorderStyle = 'Black',
          [ValidateRange(0,100)][int]$BorderBrightness = 100,
          [string]$ArtworkPath,
          [string]$Root = (Split-Path -Parent $PSScriptRoot))

    $bundled = Join-Path $Root 'artwork'
    if ($BorderStyle -eq 'Black') {
        return @('-artpath', ($bundled + ';artwork'), '-override_artwork', 'worldborders', '-view', 'Black')
    }
    $artPath = Join-Path $Root '.local/world/artwork'
    $package = 'worldcustom'
    if ($BorderStyle -eq 'CustomArt' -and $ArtworkPath) {
        $digest = (Get-FileHash -LiteralPath $ArtworkPath -Algorithm SHA256 -ErrorAction Stop).Hash
        $package = 'worldcustom-hi-' + $digest.ToLowerInvariant()
    }
    $folder = Join-Path $artPath $package
    [void][IO.Directory]::CreateDirectory($folder)
    $panorama = $false
    if ($BorderStyle -eq 'CustomArt') {
        if (![IO.File]::Exists((Join-Path $folder 'imported.png'))) {
            if (!$ArtworkPath) { throw 'Import a JPEG or PNG before selecting Custom art.' }
            ConvertTo-WorldBorderArt -Path $ArtworkPath -Folder $folder
        }
        $asset = 'imported.png'
        Add-Type -AssemblyName System.Drawing
        $image = [Drawing.Image]::FromFile((Join-Path $folder $asset))
        try { $panorama = $image.Width -gt $image.Height } finally { $image.Dispose() }
    } else {
        $asset = if ($BorderStyle -eq 'PressureHull') { 'hull.svg' } else { 'ocean.svg' }
        [IO.File]::Copy((Join-Path $bundled "worldborders/$asset"), (Join-Path $folder $asset), $true)
    }
    $level = ($BorderBrightness / 100).ToString('0.00', [Globalization.CultureInfo]::InvariantCulture)
    $tint = "<color red='$level' green='$level' blue='$level'/>"
    $sides = if ($panorama) {
        "<element ref='art'><bounds x='-160' y='0' width='640' height='240'/>$tint</element>"
    } else {
        "<element ref='art'><bounds x='-160' y='0' width='160' height='240'/>$tint</element>
         <element ref='art'><bounds x='320' y='0' width='160' height='240'/><orientation flipx='yes'/>$tint</element>"
    }
    $layout = "<mamelayout version='2'><element name='art'><image file='$asset'/></element>
        <view name='$BorderStyle'>$sides
        <screen index='0'><bounds x='0' y='0' width='320' height='240'/></screen></view></mamelayout>"
    $temporary = Join-Path $folder ([guid]::NewGuid().ToString() + '.tmp')
    try {
        [IO.File]::WriteAllText($temporary, $layout)
        Install-WorldBorderFile $temporary (Join-Path $folder 'default.lay')
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
    @('-artpath', ($artPath + ';' + $bundled + ';artwork'), '-override_artwork', $package, '-view', $BorderStyle, '-artwork_crop')
}
