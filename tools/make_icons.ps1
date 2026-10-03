# SPDX-License-Identifier: GPL-3.0-or-later
# Renders the app and document icons from their SVGs (spec 9) with Edge in
# headless mode, which supports the original's filters, then packs the .ico
# files. Run on Windows after changing an icon SVG; the outputs are committed.
#
#   pwsh tools/make_icons.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$res = Join-Path $root 'resources'
$out = Join-Path $res 'icons/app'
New-Item -ItemType Directory -Force $out | Out-Null
$edge = @("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
          "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe") |
        Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw 'Microsoft Edge is needed to render the icons.' }
$temp = Join-Path ([IO.Path]::GetTempPath()) 'leinwand-icons'
New-Item -ItemType Directory -Force $temp | Out-Null

function Render([string]$svg, [int]$size, [string]$png) {
  $html = Join-Path $temp "render-$size.html"
  $src = 'file:///' + ((Join-Path $res $svg) -replace '\\', '/')
  Set-Content -Encoding utf8 $html "<html><body style=`"margin:0;background:transparent`"><img src=`"$src`" width=`"$size`" height=`"$size`" style=`"display:block`"></body></html>"
  # Edge reports on stderr; run it as a process so that is not an error.
  $edgeArgs = @('--headless=new', '--disable-gpu', '--hide-scrollbars',
                '--force-device-scale-factor=1', '--default-background-color=00000000',
                "--window-size=$size,$size", "--screenshot=$png",
                ('file:///' + ($html -replace '\\', '/')))
  Start-Process -FilePath $edge -ArgumentList $edgeArgs -Wait -NoNewWindow `
      -RedirectStandardError (Join-Path $temp 'edge.log')
  if (-not (Test-Path $png)) { throw "Rendering $svg at $size failed." }
}

# The app: a simplified drawing up to 32 px (thin legs vanish otherwise).
foreach ($size in 16, 24, 32) { Render 'leinwand-icon-small.svg' $size (Join-Path $out "leinwand-$size.png") }
foreach ($size in 48, 64, 128, 256, 512) { Render 'leinwand-icon.svg' $size (Join-Path $out "leinwand-$size.png") }
# .lwd documents.
foreach ($size in 16, 24, 32, 48, 256) { Render 'leinwand-document.svg' $size (Join-Path $out "document-$size.png") }

# .ico files with PNG images (Windows Vista and later read these).
function Pack([string[]]$pngs, [string]$ico) {
  # The comma keeps each file's bytes as one array in the pipeline.
  $images = @($pngs | ForEach-Object { , [IO.File]::ReadAllBytes($_) })
  $stream = New-Object IO.MemoryStream
  $w = New-Object IO.BinaryWriter $stream
  $w.Write([UInt16]0); $w.Write([UInt16]1); $w.Write([UInt16]$images.Count)
  $offset = 6 + 16 * $images.Count
  for ($i = 0; $i -lt $images.Count; $i++) {
    $bytes = $images[$i]
    # Width and height from the PNG header (big-endian, at 16 and 20).
    $width = ($bytes[16] -shl 24) -bor ($bytes[17] -shl 16) -bor ($bytes[18] -shl 8) -bor $bytes[19]
    $height = ($bytes[20] -shl 24) -bor ($bytes[21] -shl 16) -bor ($bytes[22] -shl 8) -bor $bytes[23]
    $w.Write([byte]($(if ($width -ge 256) { 0 } else { $width })))
    $w.Write([byte]($(if ($height -ge 256) { 0 } else { $height })))
    $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([UInt16]1); $w.Write([UInt16]32)
    $w.Write([UInt32]$bytes.Length); $w.Write([UInt32]$offset)
    $offset += $bytes.Length
  }
  foreach ($bytes in $images) { $w.Write($bytes) }
  [IO.File]::WriteAllBytes($ico, $stream.ToArray())
}
Pack (16, 24, 32, 48, 256 | ForEach-Object { Join-Path $out "leinwand-$_.png" }) (Join-Path $res 'leinwand.ico')
Pack (16, 24, 32, 48, 256 | ForEach-Object { Join-Path $out "document-$_.png" }) (Join-Path $res 'leinwand-document.ico')
Write-Output 'Icons written.'
