# Launch the built notes_workspace, capture ONLY its own windows (PrintWindow),
# then close it. Never grabs the rest of the desktop (privacy + clean assets).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File capture_notes.ps1
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$exe  = Get-ChildItem -Path (Join-Path $root "examples\notes_workspace\build\windows") -Recurse -Filter "notes_workspace.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "Release" } | Select-Object -First 1
if (-not $exe) { Write-Error "exe not found"; exit 1 }

$assets = Join-Path $root "assets"
New-Item -ItemType Directory -Force -Path $assets | Out-Null

$sig = @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Win {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr l);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
Add-Type -TypeDefinition $sig

Write-Host "launching $($exe.FullName)"
$p = Start-Process -FilePath $exe.FullName -PassThru
Start-Sleep -Seconds 9   # let the surfaces spawn + settle

$pids = @{}
Get-Process | Where-Object { $_.ProcessName -eq "notes_workspace" } | ForEach-Object { $pids[[uint32]$_.Id] = $true }
Write-Host "notes_workspace pids: $($pids.Keys -join ', ')"

$script:idx = 0
$cb = [Win+EnumWindowsProc]{
  param($h,$l)
  $wpid = 0
  [void][Win]::GetWindowThreadProcessId($h, [ref]$wpid)
  if ($pids.ContainsKey([uint32]$wpid)) {
    $vis = [Win]::IsWindowVisible($h)
    $r = New-Object Win+RECT
    [void][Win]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L; $ht = $r.B - $r.T
    $sb = New-Object System.Text.StringBuilder 256
    [void][Win]::GetWindowText($h, $sb, 256)
    $title = $sb.ToString()
    Write-Host ("  hwnd window: vis={0} {1}x{2} title='{3}'" -f $vis, $w, $ht, $title)
    if ($vis -and $w -gt 120 -and $ht -gt 80) {
      $b = New-Object System.Drawing.Bitmap $w, $ht
      $gg = [System.Drawing.Graphics]::FromImage($b)
      $hdc = $gg.GetHdc()
      [void][Win]::PrintWindow($h, $hdc, 2)  # PW_RENDERFULLCONTENT
      $gg.ReleaseHdc($hdc); $gg.Dispose()
      $name = ($title -replace '[^A-Za-z0-9]+','-').Trim('-'); if (-not $name) { $name = "surface" }
      $out = Join-Path $assets ("notes-{0:d2}-{1}.png" -f $script:idx, $name)
      $b.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
      Write-Host "  -> saved $out"
      $script:idx++
    }
  }
  return $true
}
[void][Win]::EnumWindows($cb, [IntPtr]::Zero)

Start-Sleep -Seconds 1
try { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } catch {}
Get-Process -Name "notes_workspace" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Write-Host "done; captured $script:idx app windows"
