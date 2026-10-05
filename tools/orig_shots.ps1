# Run the original HP1 (HP.exe) straight into a map and capture its window every second, to compare frames with
# ours (HP1_SHOTS at the same times; the original loads a few seconds slower, so pair frames by content).
# Usage: powershell -File tools/orig_shots.ps1 -Map Lev_Tut2 [-Seconds 28] [-Out dir] [-GameDir ..\eagames\hp1-orig]
#   GameDir   a runnable copy of the original (never ../eagames/hp1); its System\HP.ini must skip the first-run wizard
#             (FirstRun=433) and run windowed (StartupFullscreen=False, D3DDrv UseFullscreen=False), Brightness 0.4
#             like ours. Its window comes to the front while it runs.
# Only the game window is captured (PrintWindow), never the desktop.
param(
	[Parameter(Mandatory = $true)][string]$Map,
	[int]$Seconds = 28,
	[string]$Out = "build\orig_shots",
	[string]$GameDir = ""
)
$ErrorActionPreference = "Stop"
if (-not $GameDir) { $GameDir = Join-Path $PSScriptRoot "..\..\eagames\hp1-orig" }
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class OrigShots {
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
	[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
"@

$sys = Join-Path (Resolve-Path $GameDir) "System"
New-Item -ItemType Directory -Force $Out | Out-Null
# A leftover Running.ini (the game was killed) starts it in Recovery Mode.
Remove-Item "$sys\Running.ini" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "$sys\HP.exe" -ArgumentList "$Map.unr" -WorkingDirectory $sys -PassThru
$clock = [Diagnostics.Stopwatch]::StartNew()
for ($i = 1; $i -le $Seconds; $i++) {
	while ($clock.Elapsed.TotalSeconds -lt $i) { Start-Sleep -Milliseconds 20 }
	$p.Refresh()
	$h = $p.MainWindowHandle
	if ($h -eq [IntPtr]::Zero) { continue }
	$r = New-Object OrigShots+RECT
	[void][OrigShots]::GetClientRect($h, [ref]$r)
	if ($r.R -le 0 -or $r.B -le 0) { continue }
	$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
	$g = [System.Drawing.Graphics]::FromImage($bmp)
	$hdc = $g.GetHdc()
	[void][OrigShots]::PrintWindow($h, $hdc, 3) # PW_CLIENTONLY | PW_RENDERFULLCONTENT (D3D content too)
	$g.ReleaseHdc($hdc)
	$bmp.Save((Join-Path $Out ("orig_{0:D3}.png" -f $i)), [System.Drawing.Imaging.ImageFormat]::Png)
	$g.Dispose(); $bmp.Dispose()
}
Stop-Process -Id $p.Id -Force
Start-Sleep -Milliseconds 500
Remove-Item "$sys\Running.ini" -ErrorAction SilentlyContinue
"$Seconds s of $Map -> $Out"
