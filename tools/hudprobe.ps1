# hudprobe.ps1 - measure HUD flicker on the real window (ISS-033)
#
# The HUD flicker was only visible on screen, so this is the one probe that has
# to look at the composited desktop instead of a screenshot file: it starts the
# game and grabs the client area as fast as it can, N frames in a row, then
# counts near-white HUD pixels in the band the overlay text lives in.
#
#   HUD inside the back buffer -> every capture has the same ink, spread ~0
#   HUD painted on the window DC after SwapBuffers -> text survives some frames
#   and not others, so the per-frame count swings and whole captures come in
#   near-empty.
#
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File tools/hudprobe.ps1 [[-Exe path] [-Frames 40]]

param([string]$Exe = "bin\Town3D.exe", [int]$Frames = 40, [string]$Extra = "")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  public struct RECT { public int Left, Top, Right, Bottom; }
  public struct POINT { public int X, Y; }
}
"@

$argv = @("--showlog")
if ($Extra -ne "") { $argv += $Extra.Split(" ") }
$p = Start-Process -FilePath $Exe -ArgumentList $argv -WorkingDirectory (Get-Location) -PassThru
Start-Sleep -Seconds 4                       # let it build a town and start drawing

if ($p.HasExited) { throw "EXITED early, code=$($p.ExitCode)" }
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Stop-Process -Id $p.Id -Force; throw "no main window" }

# client area in screen coordinates (the overlay is laid out from the top-left
# of the client area, so measure exactly that)
$cr = New-Object Win32+RECT
[void][Win32]::GetClientRect($h, [ref]$cr)
$pt = New-Object Win32+POINT
[void][Win32]::ClientToScreen($h, [ref]$pt)
$x0 = $pt.X; $y0 = $pt.Y; $cw = $cr.Right; $ch = $cr.Bottom
$bandW = [Math]::Min(900, $cw); $bandH = [Math]::Min(72, $ch)
"client ${cw}x$ch at ($x0,$y0), HUD band ${bandW}x${bandH}"

$counts = @()
$bitmap = New-Object System.Drawing.Bitmap($bandW, $bandH, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
$gfx = [System.Drawing.Graphics]::FromImage($bitmap)
for ($f = 0; $f -lt $Frames; ++$f) {
    $gfx.CopyFromScreen($x0, $y0, 0, 0, (New-Object System.Drawing.Size($bandW, $bandH)))
    $ink = 0
    for ($y = 0; $y -lt $bandH; $y += 2) {
        for ($x = 0; $x -lt $bandW; $x += 2) {
            $c = $bitmap.GetPixel($x, $y)
            if ($c.R -gt 205 -and $c.G -gt 205 -and $c.B -gt 190) { ++$ink }
        }
    }
    $counts += $ink
}
$gfx.Dispose(); $bitmap.Dispose()

$sorted = $counts | Sort-Object
$med = $sorted[[int]($sorted.Count / 2)]
$lo = ($counts | Measure-Object -Minimum).Minimum
$hi = ($counts | Measure-Object -Maximum).Maximum
$avg = ($counts | Measure-Object -Average).Average
# "present" = the capture carries a real overlay. The digits that legitimately
# change between frames (fps, clock, pos) are a few hundred px at most, never a
# third of the text, so a frame below a third of the fullest one has no HUD on it.
$present = @($counts | Where-Object { $_ -gt 0.35 * $hi }).Count

"per-frame HUD ink (sampled every 2 px, $Frames captures back to back):"
($counts -join " ")
"max $hi  median $med  mean $([math]::Round($avg,1))  min $lo  spread $($hi-$lo)  HUD present on $present of $Frames captures"
if ($hi -eq 0) { "VERDICT: the overlay never reaches the screen at all" }
elseif ($present -lt $Frames) { "VERDICT: FLICKERS - the overlay is on $present of $Frames captures, missing on $($Frames-$present)" }
elseif ($hi - $lo -gt 0.25 * $hi) { "VERDICT: UNSTABLE - ink swings $lo..$hi from frame to frame" }
else { "VERDICT: stable - the overlay is on every frame" }

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
