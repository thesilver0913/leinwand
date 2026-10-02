# SPDX-License-Identifier: GPL-3.0-or-later
# Checks the Windows build toolchain for Leinwand and installs what is missing.
#   powershell -ExecutionPolicy Bypass -File tools\setup-windows.ps1 -CheckOnly   # report only
#   powershell -ExecutionPolicy Bypass -File tools\setup-windows.ps1              # install
# Installing needs an elevated PowerShell (Visual Studio requires it).
param([switch]$CheckOnly)

$QtSeries = '6.8'
$QtRoot = 'C:\Qt'
$VcpkgRoot = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { "$env:USERPROFILE\vcpkg" }
$VsInstaller = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$VsWorkload = 'Microsoft.VisualStudio.Workload.NativeDesktop'

function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
              [Environment]::GetEnvironmentVariable('Path', 'User')
}

# Present if the command is on PATH (any install source) or winget knows the package.
function Test-WingetPackage($id, $command) {
  if ($command -and (Get-Command $command -ErrorAction SilentlyContinue)) { return $true }
  winget list --id $id --exact --accept-source-agreements *> $null
  return $LASTEXITCODE -eq 0
}

function Find-VisualStudio {
  if (-not (Test-Path "$VsInstaller\vswhere.exe")) { return $null }
  & "$VsInstaller\vswhere.exe" -products * -version '[17,18)' -property installationPath |
    Select-Object -First 1
}

function Test-VsWorkload {
  if (-not (Test-Path "$VsInstaller\vswhere.exe")) { return $false }
  $found = & "$VsInstaller\vswhere.exe" -products * -version '[17,18)' -requires $VsWorkload `
           -property installationPath
  return [bool]$found
}

function Find-Qt {
  if ($env:QT_ROOT_DIR -and (Test-Path "$env:QT_ROOT_DIR\bin\qmake.exe")) { return $env:QT_ROOT_DIR }
  Get-ChildItem "$QtRoot\$QtSeries.*\msvc2022_64\bin\qmake.exe" -ErrorAction SilentlyContinue |
    Select-Object -Last 1 | ForEach-Object { $_.Directory.Parent.FullName }
}

# Each step: a name, a check that returns $true when already present, and an installer.
$winget = [ordered]@{
  # id                     = name, command that proves it is installed
  'Git.Git'                = 'Git', 'git'
  'Kitware.CMake'          = 'CMake', 'cmake'
  'Ninja-build.Ninja'      = 'Ninja', 'ninja'
  'KhronosGroup.VulkanSDK' = 'Vulkan SDK (validation layers for M0)', $null
  'Python.Python.3.12'     = 'Python (used to install Qt)', 'py'
  'OpenJS.NodeJS.LTS'      = 'Node.js (Ponytail plugin hooks)', 'node'
  'LLVM.LLVM'              = 'LLVM (clang-format, clang-tidy)', 'clang-format'
  'GitHub.cli'             = 'GitHub CLI', 'gh'
  'Anthropic.ClaudeCode'   = 'Claude Code', 'claude'
}
$steps = @()
foreach ($id in $winget.Keys) {
  $steps += [pscustomobject]@{
    Name    = $winget[$id][0]
    Check   = [scriptblock]::Create("Test-WingetPackage '$id' '$($winget[$id][1])'")
    Install = [scriptblock]::Create(
      "winget install --id '$id' --exact --silent --accept-package-agreements --accept-source-agreements")
  }
}
$steps += [pscustomobject]@{
  Name    = 'Visual Studio 2022 + Desktop development with C++'
  Check   = { Test-VsWorkload }
  Install = {
    $vs = Find-VisualStudio
    if ($vs) {
      & "$VsInstaller\setup.exe" modify --installPath "$vs" --add $VsWorkload `
        --includeRecommended --passive --norestart | Out-Default
    } else {
      winget install --id Microsoft.VisualStudio.2022.Community --exact --silent `
        --accept-package-agreements --accept-source-agreements `
        --override "--add $VsWorkload --includeRecommended --passive --norestart --wait"
    }
  }
}
$steps += [pscustomobject]@{
  Name    = "vcpkg ($VcpkgRoot)"
  Check   = { Test-Path "$VcpkgRoot\vcpkg.exe" }
  Install = {
    if (-not (Test-Path $VcpkgRoot)) { git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot }
    & "$VcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
  }
}
$steps += [pscustomobject]@{
  Name    = "Qt $QtSeries (MSVC 2022 64-bit)"
  Check   = { [bool](Find-Qt) }
  Install = {
    # Official LGPL binaries via aqtinstall; no Qt account needed.
    py -m pip install --user --upgrade aqtinstall
    $version = (py -m aqt list-qt windows desktop --spec $QtSeries --latest-version).Trim()
    py -m aqt install-qt windows desktop $version win64_msvc2022_64 -O $QtRoot
  }
}

Refresh-Path
$missing = @()
foreach ($step in $steps) {
  $ok = & $step.Check
  Write-Host ("[{0}] {1}" -f $(if ($ok) { 'ok' } else { '--' }), $step.Name)
  if (-not $ok) { $missing += $step }
}
if ($CheckOnly -or $missing.Count -eq 0) {
  Write-Host "`n$($missing.Count) missing."
} else {
  $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).
           IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
  if (-not $admin) { throw 'Run this from an elevated PowerShell to install.' }
  foreach ($step in $missing) {
    Write-Host "`n== Installing $($step.Name)"
    & $step.Install
    Refresh-Path
  }
}

# Environment variables the CMake presets read.
if (-not $CheckOnly) {
  if (Test-Path "$VcpkgRoot\vcpkg.exe") {
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, 'User')
  }
  $qt = Find-Qt
  if ($qt) { [Environment]::SetEnvironmentVariable('QT_ROOT_DIR', $qt, 'User') }
  # The LLVM installer does not put clang-format on PATH by default.
  $llvm = "$env:ProgramFiles\LLVM\bin"
  $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
  if ((Test-Path $llvm) -and ($env:Path -split ';') -notcontains $llvm) {
    [Environment]::SetEnvironmentVariable('Path', "$userPath;$llvm", 'User')
  }
  Write-Host "`nVCPKG_ROOT=$VcpkgRoot  QT_ROOT_DIR=$qt"
  Write-Host 'Open a new "x64 Native Tools Command Prompt for VS 2022" so the changes apply.'
}
