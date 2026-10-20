# Web98 package: stages Release build, generates banner.bmp + files.nsh,
# builds installer with NSIS, optional silent install/uninstall test.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File scripts\package.ps1 [-Test]
param([switch]$Test)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$src  = Join-Path $root 'native\build\Release'
$inst = Join-Path $root 'installer'
$staged = Join-Path $inst 'staged'
$dist = Join-Path $root 'dist'
$makensis = 'C:\Program Files (x86)\NSIS\makensis.exe'

if (-not (Test-Path (Join-Path $src 'web98.exe'))) { throw "Release build not found: $src" }
if (-not (Test-Path $makensis)) { throw "makensis not found: $makensis" }

# ---------------------------------------------------------------- stage
if (Test-Path $staged) { Remove-Item $staged -Recurse -Force }
New-Item -ItemType Directory -Path $staged, $dist -Force | Out-Null
$exclude = @('web98_unit.exe', 'sqlite3.lib', 'web98.lib', 'web98.exp')
$files = Get-ChildItem $src -Recurse -File | Where-Object { $exclude -notcontains $_.Name }
foreach ($f in $files) {
  $rel = $f.FullName.Substring($src.Length + 1)
  $dst = Join-Path $staged $rel
  New-Item -ItemType Directory -Path (Split-Path $dst) -Force | Out-Null
  Copy-Item $f.FullName $dst
}
Write-Host "staged $($files.Count) files -> installer\staged"

# ---------------------------------------------------------------- banner.bmp
Add-Type -AssemblyName System.Drawing
$bw, $bh = 164, 263
$bmp = New-Object System.Drawing.Bitmap($bw, $bh)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$rect = New-Object System.Drawing.Rectangle(0, 0, $bw, $bh)
$brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
  $rect,
  [System.Drawing.Color]::FromArgb(0, 0, 128),
  [System.Drawing.Color]::FromArgb(106, 180, 236),
  [System.Drawing.Drawing2D.LinearGradientMode]::Vertical
)
$g.FillRectangle($brush, 0, 0, $bw, $bh)
try {
  $drawn = $false
  $icoPath = Join-Path $root 'build\icon.ico'
  foreach ($sz in @(32, 16, 48)) {
    try {
      $ico = New-Object System.Drawing.Icon($icoPath, $sz, $sz)
      $ib = $ico.ToBitmap()
      $off = [int]((48 - $sz) / 2)
      $g.DrawImage($ib, (New-Object System.Drawing.Rectangle(58 + $off, 30 + $off, $sz, $sz)))
      $ib.Dispose()
      $ico.Dispose()
      $drawn = $true
      break
    } catch { }
  }
  if (-not $drawn) {
    # .NET ToBitmap fails on PNG-encoded entries — decode PNG payload manually
    $b = [IO.File]::ReadAllBytes($icoPath)
    $n = [BitConverter]::ToUInt16($b, 4)
    $cand = -1; $candArea = 0
    for ($i = 0; $i -lt $n; $i++) {
      $o = 6 + $i * 16
      $w = [int]$b[$o]; if ($w -eq 0) { $w = 256 }
      $h = [int]$b[$o + 1]; if ($h -eq 0) { $h = 256 }
      $imgOff = [BitConverter]::ToUInt32($b, $o + 12)
      if (($imgOff + 8) -ge $b.Length) { continue }
      $isPng = ($b[$imgOff] -eq 0x89 -and $b[$imgOff + 1] -eq 0x50 -and $b[$imgOff + 2] -eq 0x4E -and $b[$imgOff + 3] -eq 0x47)
      if ($isPng -and $w -le 256 -and ($w * $h) -gt $candArea) { $candArea = $w * $h; $cand = $o }
    }
    if ($cand -ge 0) {
      $imgLen = [BitConverter]::ToUInt32($b, $cand + 8)
      $imgOff = [BitConverter]::ToUInt32($b, $cand + 12)
      [byte[]]$png = $b[$imgOff..($imgOff + $imgLen - 1)]
      $ms = New-Object System.IO.MemoryStream(,$png)
      $ib = [System.Drawing.Image]::FromStream($ms)
      $g.DrawImage($ib, (New-Object System.Drawing.Rectangle(58, 30, 48, 48)))
      $ib.Dispose(); $ms.Dispose()
      $drawn = $true
    }
  }
  if (-not $drawn) { Write-Warning 'icon not drawn in banner (skipping)' }
} catch {
  Write-Warning "icon draw failed: $_"
}
$titleFont = New-Object System.Drawing.Font('MS Sans Serif', 14, [System.Drawing.FontStyle]::Bold)
$subFont   = New-Object System.Drawing.Font('MS Sans Serif', 9, [System.Drawing.FontStyle]::Regular)
$white = [System.Drawing.Brushes]::White
$g.DrawString('Web98', $titleFont, $white, 12, 196)
$g.DrawString('Setup', $subFont, $white, 14, 226)
$g.Dispose()
$bmp.Save((Join-Path $inst 'banner.bmp'), [System.Drawing.Imaging.ImageFormat]::Bmp)
$bmp.Dispose()
Write-Host 'banner.bmp generated'

# ---------------------------------------------------------------- files.nsh
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('!define TOTALFILES "' + $files.Count + '"')
$lines.Add('!macro InstallFiles')
$lastDir = $null
foreach ($f in $files) {
  $rel = $f.FullName.Substring($src.Length + 1)
  $dirRel = Split-Path $rel -Parent
  $outDir = if ($dirRel) { '$INSTDIR\' + $dirRel } else { '$INSTDIR' }
  if ($outDir -ne $lastDir) {
    $lines.Add('  SetOutPath "' + $outDir + '"')
    $lastDir = $outDir
  }
  $lines.Add('  File "/oname=' + $f.Name + '" "staged\' + $rel + '"')
  $lines.Add('  !insertmacro AdvFile')
}
$lines.Add('  SetOutPath "$INSTDIR"')
$lines.Add('!macroend')
Set-Content -Path (Join-Path $inst 'files.nsh') -Value $lines -Encoding UTF8
Write-Host "files.nsh generated ($($files.Count) entries)"

# ---------------------------------------------------------------- makensis
& $makensis /V2 (Join-Path $inst 'web98.nsi')
if ($LASTEXITCODE -ne 0) { throw "makensis failed: $LASTEXITCODE" }
$setup = Join-Path $dist 'Web98 Setup 0.1.0 x64.exe'
if (-not (Test-Path $setup)) { throw "setup not produced: $setup" }
$sizeMB = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "SETUP OK: $setup ($sizeMB MB)"

# ---------------------------------------------------------------- test
if ($Test) {
  $testDir = Join-Path $env:TEMP 'web98-install-test'
  if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force }
  Write-Host 'silent install...'
  $p = Start-Process -FilePath $setup -ArgumentList "/S", "/D=$testDir" -PassThru
  $p.WaitForExit(180000) | Out-Null
  if (-not $p.HasExited) { $p.Kill(); throw 'silent install timeout' }
  Write-Host "install exit=$($p.ExitCode)"
  $required = @('web98.exe', 'web98.dll', 'libcef.dll', 'Uninstall.exe', 'icudtl.dat', 'locales\en-US.pak', 'res\src\renderer\index.html')
  $fail = @()
  foreach ($r in $required) {
    if (-not (Test-Path (Join-Path $testDir $r))) { $fail += $r }
  }
  if ($fail.Count) { throw "missing installed files: $($fail -join ', ')" }
  Write-Host "install verified: $($required.Count) required files present"
  Write-Host 'silent uninstall...'
  $u = Start-Process -FilePath (Join-Path $testDir 'Uninstall.exe') -ArgumentList '/S' -PassThru
  $u.WaitForExit(120000) | Out-Null
  if (-not $u.HasExited) { $u.Kill(); throw 'silent uninstall timeout' }
  Start-Sleep -Seconds 2
  if (Test-Path $testDir) { Remove-Item $testDir -Recurse -Force -ErrorAction SilentlyContinue }
  if (Test-Path $testDir) { Write-Warning "test dir still present: $testDir" } else { Write-Host 'uninstall verified: dir removed' }
}
