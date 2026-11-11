. (Join-Path $PSScriptRoot 'world_borders.ps1')

function Get-WorldArtPresets {
    param([string]$Root)
    $directory = Join-Path $Root '.local/art-sources'
    foreach ($entry in @(
        @('FanBackground','Fan background','world-fan-background.jpg')
    )) {
        $path = Join-Path $directory $entry[2]
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            [pscustomobject]@{Style=$entry[0]; Name=$entry[1]; Path=$path}
        }
    }
}

function Get-WorldMenuSettings {
    param([string]$Root, [string]$DisplayStyle, [string]$BorderStyle,
          [ValidateRange(0,100)][int]$Brightness = 100)
    $path = Join-Path $Root '.local/world/presentation.json'
    $saved = if (Test-Path -LiteralPath $path) {
        try { Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { $null }
    }
    if (!$DisplayStyle) { $DisplayStyle = $saved.DisplayStyle }
    if (!$BorderStyle) {
        $BorderStyle = $saved.BorderStyle
        if ($BorderStyle -eq 'CustomArt' -and !(Test-WorldBorderArt -Root $Root)) { $BorderStyle = 'Black' }
    } elseif ($BorderStyle -eq 'CustomArt' -and !(Test-WorldBorderArt -Root $Root)) {
        throw 'Import an image from the launcher Options before selecting CustomArt.'
    }
    if (!$PSBoundParameters.ContainsKey('Brightness') -and $null -ne $saved.Brightness) {
        $savedValue = 0
        if ([int]::TryParse([string]$saved.Brightness,[ref]$savedValue) -and $savedValue -ge 0 -and $savedValue -le 100) {
            $Brightness = $savedValue
        }
    }
    if ($DisplayStyle -notin @('Pixels','FlatCRT','FlatCRTFast')) { $DisplayStyle = 'Pixels' }
    $available = @('Black','PressureHull','DeepWater','CustomArt') + @(Get-WorldArtPresets -Root $Root | ForEach-Object Style)
    if ($BorderStyle -notin $available) {
        if ($PSBoundParameters.ContainsKey('BorderStyle')) {
            throw "Artwork for '$BorderStyle' is unavailable. Run tools/get_border_art.ps1 to download the cabinet artwork."
        }
        $BorderStyle = 'Black'
    }
    [pscustomobject]@{DisplayStyle=$DisplayStyle; BorderStyle=$BorderStyle; Brightness=$Brightness}
}
function Show-WorldMenu {
    param([string]$Root, [string]$DisplayStyle, [string]$BorderStyle,
          [ValidateRange(0,100)][int]$Brightness = 100)
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    . (Join-Path $PSScriptRoot 'world_menu_input.ps1')
    [Windows.Forms.Application]::EnableVisualStyles()
    $settings = Join-Path $Root '.local/world/presentation.json'
    $current = Get-WorldMenuSettings @PSBoundParameters
    $styles = @('Pixels','FlatCRT','FlatCRTFast')
    $presets = @(Get-WorldArtPresets -Root $Root)
    $borders = @('Black','PressureHull','DeepWater') + @($presets | ForEach-Object Style) + 'CustomArt'
    $DisplayStyle = $current.DisplayStyle
    $BorderStyle = $current.BorderStyle
    $form = New-Object Windows.Forms.Form
    $form.Text = 'In the Hunt'
    $form.StartPosition = 'CenterScreen'
    $form.FormBorderStyle = 'FixedDialog'
    $form.MaximizeBox = $false
    $form.AutoScaleMode = 'Font'
    $form.AutoSize = $true
    $form.AutoSizeMode = 'GrowAndShrink'
    $form.Font = New-Object Drawing.Font('Segoe UI',12)
    $form.BackColor = [Drawing.ColorTranslator]::FromHtml('#14272a')
    $form.ForeColor = [Drawing.ColorTranslator]::FromHtml('#ece4cc')
    $layout = New-Object Windows.Forms.FlowLayoutPanel
    $layout.AutoSize = $true
    $layout.FlowDirection = 'TopDown'
    $layout.WrapContents = $false
    $layout.Padding = New-Object Windows.Forms.Padding(26)
    $form.Controls.Add($layout)
    $heading = New-Object Windows.Forms.Label
    $heading.AutoSize = $true
    $heading.Text = 'IN THE HUNT'
    $heading.ForeColor = [Drawing.ColorTranslator]::FromHtml('#e9c088')
    $heading.Font = New-Object Drawing.Font('Georgia',24,[Drawing.FontStyle]::Bold)
    $layout.Controls.Add($heading)
    $hint = New-Object Windows.Forms.Label
    $hint.AutoSize = $true
    $hint.Text = "D-pad: move / change    A: select    B: back"
    $layout.Controls.Add($hint)

    function New-MenuButton($text) {
        $button = New-Object Windows.Forms.Button
        $button.Text = $text
        $button.Size = New-Object Drawing.Size(320,44)
        $button.FlatStyle = 'Flat'
        $button.ForeColor = $form.ForeColor
        $button.BackColor = [Drawing.ColorTranslator]::FromHtml('#284344')
        $layout.Controls.Add($button)
        return $button
    }
    $start = New-MenuButton '&Start'
    $options = New-MenuButton '&Options'
    function New-MenuChoice($caption, $labels, $values, $current) {
        $label = New-Object Windows.Forms.Label
        $label.Text = $caption
        $label.AutoSize = $true
        $layout.Controls.Add($label)
        $choice = New-Object Windows.Forms.ComboBox
        $choice.AccessibleName = $caption
        $choice.DropDownStyle = 'DropDownList'
        $choice.Width = 320
        $choice.Items.AddRange($labels)
        $choice.SelectedIndex = [Math]::Max(0, [array]::IndexOf($values,$current))
        $layout.Controls.Add($choice)
        $label.Visible = $false
        $choice.Visible = $false
        return $choice
    }
    $shader = New-MenuChoice 'Display' @('Integer pixels','Flat CRT Deluxe','Flat CRT Fast') $styles $DisplayStyle
    $borderLabels = @('Black','Pressure hull','Deep water') + @($presets | ForEach-Object Name)
    if (Test-WorldBorderArt -Root $Root) { $borderLabels += 'Imported artwork' }
    $border = New-MenuChoice 'Cabinet border' $borderLabels $borders $BorderStyle
    $import = New-MenuButton '&Import border artwork...'
    $import.Visible = $false
    $import.Add_Click({
        $dialog = New-Object Windows.Forms.OpenFileDialog
        $dialog.Title = 'Import border artwork'
        $dialog.Filter = 'Border images (*.png, *.jpg, *.jpeg)|*.png;*.jpg;*.jpeg'
        $sourceArt = Join-Path $Root '.local/art-sources'
        if (Test-Path -LiteralPath $sourceArt -PathType Container) { $dialog.InitialDirectory = $sourceArt }
        try {
            if ($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK) {
                Import-WorldBorderArt -Root $Root -Path $dialog.FileName | Out-Null
                if ($border.Items.Count -lt $borders.Count) { [void]$border.Items.Add('Imported artwork') }
                $border.SelectedIndex = $borders.Count - 1
            }
        } catch {
            [void][Windows.Forms.MessageBox]::Show($form,$_.Exception.Message,'Import border artwork')
        } finally { $dialog.Dispose() }
    })
    $brightnessLabel = New-Object Windows.Forms.Label
    $brightnessLabel.Text = 'Border brightness (%)'
    $brightnessLabel.AutoSize = $true
    $brightnessLabel.Visible = $false
    $layout.Controls.Add($brightnessLabel)
    $level = New-Object Windows.Forms.NumericUpDown
    $level.AccessibleName = $brightnessLabel.Text
    $level.Width = 320
    $level.Value = $current.Brightness
    $level.Visible = $false
    $layout.Controls.Add($level)
    $controls = New-MenuButton 'In-game options and &controls'
    $controls.Visible = $false
    $exitButton = New-MenuButton 'E&xit'
    $menuOrder = @($layout.Controls)
    $form.Tag = 'Exit'
    $start.Add_Click({ $form.Tag = 'Start'; $form.Close() })
    $controls.Add_Click({ $form.Tag = 'Options'; $form.Close() })
    $options.Add_Click({
        $show = !$controls.Visible
        $layout.SuspendLayout()
        try {
            foreach ($item in $menuOrder) {
                if ($item -is [Windows.Forms.ComboBox] -or
                    ($item -is [Windows.Forms.Label] -and $item -ne $heading -and $item -ne $hint)) { $item.Visible = $show }
            }
            $controls.Visible = $show
            $level.Visible = $show
            $import.Visible = $show
            for ($i = 0; $i -lt $menuOrder.Count; $i++) { $layout.Controls.SetChildIndex($menuOrder[$i],$i) }
        } finally {
            $layout.ResumeLayout($true)
        }
        if ($show) { $shader.Focus() | Out-Null }
    })
    $exitButton.Add_Click({ $form.Close() })
    $form.CancelButton = $exitButton
    $timer = Start-WorldMenuPad $form $options $controls
    try {
        [void]$form.ShowDialog()
        $result = [pscustomobject]@{Action=$form.Tag; DisplayStyle=$styles[$shader.SelectedIndex]; BorderStyle=$borders[$border.SelectedIndex]; Brightness=[int]$level.Value}
        $temporary = "$settings.$PID.tmp"
        [IO.File]::WriteAllText($temporary, ($result | Select-Object DisplayStyle,BorderStyle,Brightness | ConvertTo-Json))
        if ([IO.File]::Exists($settings)) { [IO.File]::Replace($temporary,$settings,[System.Management.Automation.Language.NullString]::Value) }
        else { [IO.File]::Move($temporary,$settings) }
        return $result
    } finally { $timer.Dispose(); $form.Dispose() }
}
