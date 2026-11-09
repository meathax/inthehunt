function Invoke-WorldMenuPadAction {
    param($Form, $OptionsButton, $ControlsButton, [int]$Pressed)
    $current = $Form.ActiveControl
    if ($Pressed -band 0x2000) {
        if ($ControlsButton.Visible) {
            $OptionsButton.PerformClick()
            [void]$OptionsButton.Focus()
        } else {
            $Form.Close()
        }
    } elseif ($Pressed -band 3) {
        [void]$Form.SelectNextControl($current, [bool]($Pressed -band 2), $true, $true, $true)
    } elseif ($current -is [Windows.Forms.ComboBox] -and ($Pressed -band 0x100c)) {
        $delta = if ($Pressed -band 4) { -1 } else { 1 }
        $count = $current.Items.Count
        $current.SelectedIndex = ($current.SelectedIndex + $delta + $count) % $count
    } elseif ($current -is [Windows.Forms.NumericUpDown] -and ($Pressed -band 12)) {
        $delta = if ($Pressed -band 4) { -5 } else { 5 }
        $current.Value = [Math]::Min($current.Maximum,[Math]::Max($current.Minimum,$current.Value + $delta))
    } elseif (($Pressed -band 0x1000) -and $current -is [Windows.Forms.Button]) {
        $current.PerformClick()
    }
}

function Start-WorldMenuPad {
    param($Form, $OptionsButton, $ControlsButton)
    if (!('InTheHunt.MenuPad' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace InTheHunt {
    public static class MenuPad {
        [StructLayout(LayoutKind.Sequential)]
        private struct State {
            public uint packet;
            public ushort buttons;
            public byte leftTrigger, rightTrigger;
            public short lx, ly, rx, ry;
        }
        [DllImport("xinput1_4.dll")]
        private static extern uint XInputGetState(uint index, out State state);
        public static int Read(uint index) {
            State state;
            if (XInputGetState(index, out state) != 0) return -1;
            int buttons = state.buttons;
            if (state.ly > 18000) buttons |= 1;
            if (state.ly < -18000) buttons |= 2;
            if (state.lx < -18000) buttons |= 4;
            if (state.lx > 18000) buttons |= 8;
            return buttons;
        }
    }
}
'@
    }
    $previous = [int[]]@(-1,-1,-1,-1)
    $dispatch = ${function:Invoke-WorldMenuPadAction}
    $timer = New-Object Windows.Forms.Timer
    $timer.Interval = 40
    $timer.Add_Tick({
        if (!$Form.ContainsFocus) {
            for ($index = 0; $index -lt 4; $index++) { $previous[$index] = -1 }
            return
        }
        for ($index = 0; $index -lt 4; $index++) {
            $held = [InTheHunt.MenuPad]::Read($index)
            $last = $previous[$index]
            $previous[$index] = $held
            if ($held -ge 0 -and $last -ge 0) {
                $pressed = $held -band (-bnot $last)
                if ($pressed) { & $dispatch $Form $OptionsButton $ControlsButton $pressed }
            }
        }
    }.GetNewClosure())
    $timer.Start()
    return $timer
}
