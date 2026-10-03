# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the Windows release files (spec 9) from a finished release build:
#   Leinwand-<version>-windows-x64.zip         the program, for the web installer
#   Leinwand-<version>-windows-x64.zip.sha256  its SHA-256, checked by the installer
#   LeinwandSetup.exe                          the web installer (needs Inno Setup 6)
#
#   pwsh tools/package-windows.ps1 [-Build build/windows-release] [-Out dist]
param(
  [string]$Build = 'build/windows-release',
  [string]$Out = 'dist',
  # For testing the installer against a local server instead of GitHub.
  [string]$ReleasesUrl = ''
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cmakeLists = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(Leinwand VERSION ([0-9.]+)') { throw 'No version in CMakeLists.txt.' }
$version = $Matches[1]
$name = "Leinwand-$version-windows-x64"
$stage = Join-Path $Out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $Out | Out-Null

cmake --install $Build --prefix $stage
if ($LASTEXITCODE -ne 0) { throw 'cmake --install failed.' }

$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip
$hash = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLowerInvariant()
Set-Content -NoNewline -Encoding ascii "$zip.sha256" "$hash  $name.zip"
Write-Output "Package: $zip ($hash)"

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } |
        Select-Object -First 1
if (-not $iscc) {
  Write-Warning 'Inno Setup 6 was not found; the web installer was not built.'
  exit 0
}
$defines = @("/DAppVersion=$version", "/O$(Resolve-Path $Out)")
if ($ReleasesUrl) { $defines += "/DReleasesUrl=$ReleasesUrl" }
& $iscc @defines (Join-Path $root 'installer/leinwand.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed.' }
Write-Output "Installer: $(Join-Path $Out 'LeinwandSetup.exe')"
