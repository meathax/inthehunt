function Get-WorldDisplayArguments {
    param([string]$Root, [ValidateSet('Pixels','FlatCRT','FlatCRTFast')][string]$Style = 'Pixels')
    $chain = 'unfiltered'
    if ($Style -ne 'Pixels') {
        $folder = Join-Path $Root '.local/mame/bgfx/chains'
        $source = if ($Style -eq 'FlatCRT') { 'crt-geom-deluxe' } else { 'crt-geom' }
        $chain = "inthunt-flat-$source"
        $definition = Get-Content -Raw (Join-Path $folder "$source.json") | ConvertFrom-Json
        $constants = @{ curvature = @(0.0); cornersize = @(0.0); angle = @(0.0,0.0); overscan = @(1.0,1.0) }
        $definition.sliders = @($definition.sliders | Where-Object { !$constants.ContainsKey($_.name) })
        foreach ($pass in $definition.passes) {
            foreach ($uniform in $pass.uniforms) {
                if ($uniform.slider -and $constants.ContainsKey($uniform.slider)) {
                    $value = $constants[$uniform.slider]
                    $uniform.PSObject.Properties.Remove('slider')
                    $uniform | Add-Member -NotePropertyName value -NotePropertyValue $value
                }
            }
        }
        $definition.name = "In the Hunt - $Style"
        $json = $definition | ConvertTo-Json -Depth 32
        [IO.File]::WriteAllText((Join-Path $folder "$chain.json"), $json)
    }
    @('-video','bgfx','-bgfx_screen_chains',$chain,'-nofilter','-nounevenstretch',
      '-nounevenstretchx','-nounevenstretchy','-noautostretchxy','-plugins','-plugin','hiscore')
}
