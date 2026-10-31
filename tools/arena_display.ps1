# Window mode stays ON in BWAPI. Resize only the two arena game windows after
# BWAPI has installed its windowed renderer (WS_CAPTION is then present).
function Initialize-ArenaDisplay {
    if (-not ('ArenaDisplayNative' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ArenaDisplayNative {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int index);
    [DllImport("user32.dll")] public static extern int SetWindowLong(IntPtr h, int index, int value);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll", SetLastError=true)] public static extern bool SetWindowPos(
        IntPtr h, IntPtr after, int x, int y, int width, int height, uint flags);
}
'@
    }
    [ArenaDisplayNative]::SetProcessDPIAware() | Out-Null
    Add-Type -AssemblyName System.Windows.Forms
}

function Set-ArenaBorderless([string[]]$Runtimes, $Bounds) {
    $done = @{}
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    while ([DateTime]::UtcNow -lt $deadline) {
        foreach ($game in @(Get-Process -Name StarCraft -ErrorAction SilentlyContinue)) {
            if ($done.ContainsKey($game.Id)) { continue }
            # Do not touch unrelated StarCraft installations.
            if (-not $game.Path -or (Split-Path -Parent $game.Path) -notin $Runtimes) { continue }
            $game.Refresh()
            $handle = $game.MainWindowHandle
            if ($handle -eq [IntPtr]::Zero) { continue }
            $style = [ArenaDisplayNative]::GetWindowLong($handle, -16)
            if (($style -band 0x00C00000) -ne 0x00C00000) { continue }
            [ArenaDisplayNative]::ShowWindow($handle, 9) | Out-Null
            # Remove caption/resize border; retain WS_SYSMENU for BWAPI WMode.
            [ArenaDisplayNative]::SetWindowLong($handle, -16, ($style -band (-bnot 0x00C40000))) | Out-Null
            $extended = [ArenaDisplayNative]::GetWindowLong($handle, -20)
            [ArenaDisplayNative]::SetWindowLong($handle, -20, ($extended -band (-bnot 0x00000300))) | Out-Null
            if (-not [ArenaDisplayNative]::SetWindowPos($handle, [IntPtr](-2),
                $Bounds.X, $Bounds.Y, $Bounds.Width, $Bounds.Height, 0x0060)) {
                throw "Could not resize StarCraft window: PID $($game.Id)"
            }
            $done[$game.Id] = $true
        }
        if ($done.Count -eq 2) {
            Write-Host "Both games: borderless window, $($Bounds.Width)x$($Bounds.Height). Use Alt+Tab to switch."
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Borderless resize timed out: $($done.Count)/2 arena windows ready."
}
