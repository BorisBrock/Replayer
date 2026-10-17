<#
.SYNOPSIS
    Configure, compile, and package Replayer for Windows.

.DESCRIPTION
    Requirements:
      Visual Studio 2022 (Desktop development with C++), CMake 3.21+,
      Qt 6 for MSVC 2022 64-bit, and a shared FFmpeg build with MSVC
      import libraries (e.g. ffmpeg-*-win64-lgpl-shared from
      https://github.com/BtbN/FFmpeg-Builds).

    Output:
      dist\replayer-<version>-win64\   (portable folder)
      dist\replayer-<version>-win64.zip

    The version comes from -Version or REPLAYER_VERSION, else from the latest
    v* git tag (see CMakeLists.txt).

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean -QtDir D:\dev\Qt\6.11.3\msvc2022_64 -FFmpegDir D:\dev\ffmpeg
#>
param(
    [string]$QtDir = $(if ($env:QT_DIR) { $env:QT_DIR } else { 'D:\dev\Qt\6.11.3\msvc2022_64' }),
    [string]$FFmpegDir = $(if ($env:FFMPEG_DIR) { $env:FFMPEG_DIR } else { 'D:\dev\ffmpeg' }),
    [string]$Version = $env:REPLAYER_VERSION,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$ScriptDir = $PSScriptRoot
$BuildDir = Join-Path $ScriptDir 'build-release'
$DistDir = Join-Path $ScriptDir 'dist'

# ── helpers ──────────────────────────────────────────────────────────────────

function Log($msg) { Write-Host "==> $msg" }

function Invoke-Native {
    & $args[0] $args[1..($args.Count - 1)]
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

# ── checks ───────────────────────────────────────────────────────────────────

if (-not (Test-Path (Join-Path $QtDir 'bin\windeployqt.exe'))) {
    throw "Qt not found in '$QtDir' (pass -QtDir or set QT_DIR)"
}
if (-not (Test-Path (Join-Path $FFmpegDir 'lib\avcodec.lib'))) {
    throw "FFmpeg not found in '$FFmpegDir' (pass -FFmpegDir or set FFMPEG_DIR)"
}

# ── optional clean ───────────────────────────────────────────────────────────

if ($Clean) {
    Log 'Cleaning build directories...'
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $BuildDir, $DistDir
}

# ── MSVC environment ─────────────────────────────────────────────────────────

# The Qt binaries are built with MSVC 2022, so use Visual Studio 2022 even if
# a newer one is installed. Its developer shell also puts Ninja on the PATH.
if (-not $env:VCToolsInstallDir) {
    Log 'Loading the Visual Studio 2022 developer environment...'
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -version '[17.0,18.0)' -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsPath) { throw 'Visual Studio 2022 with C++ tools not found' }
    Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

# ── configure ────────────────────────────────────────────────────────────────

Log 'Configuring (Release)...'
Invoke-Native cmake -S $ScriptDir -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_PREFIX_PATH=$QtDir" `
    "-DFFMPEG_ROOT=$FFmpegDir" `
    "-DREPLAYER_VERSION=$Version"

$Version = (Get-Content (Join-Path $BuildDir 'version.txt') -Raw).Trim()
$PackageName = "replayer-$Version-win64"
$PackageDir = Join-Path $DistDir $PackageName

# ── build ────────────────────────────────────────────────────────────────────

Log 'Building...'
Invoke-Native cmake --build $BuildDir --parallel

# ── assemble the package folder ──────────────────────────────────────────────

Log 'Assembling package...'
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $PackageDir
New-Item -ItemType Directory -Force $PackageDir | Out-Null
Copy-Item (Join-Path $BuildDir 'replayer.exe') $PackageDir

# Qt libraries and plugins. The app renders with OpenGL, so the Direct3D
# shader compilers aren't needed, and it doesn't use the network. opengl32sw
# (software OpenGL) is kept as a fallback for machines without a GPU driver.
Invoke-Native (Join-Path $QtDir 'bin\windeployqt.exe') `
    --release --no-translations --no-compiler-runtime `
    --no-system-d3d-compiler --no-system-dxc-compiler `
    --skip-plugin-types 'generic,networkinformation,tls' `
    (Join-Path $PackageDir 'replayer.exe')

# MSVC runtime DLLs next to the exe, so no vc_redist install is needed
Copy-Item (Join-Path $env:VCToolsRedistDir 'x64\Microsoft.VC143.CRT\*.dll') $PackageDir

# FFmpeg libraries (swresample is a dependency of avcodec)
foreach ($lib in 'avcodec', 'avformat', 'avutil', 'swscale', 'swresample') {
    Copy-Item (Join-Path $FFmpegDir "bin\$lib-*.dll") $PackageDir
}

# License texts of Replayer, Qt and FFmpeg (see THIRD-PARTY-NOTICES.md)
Copy-Item (Join-Path $ScriptDir 'LICENSE') (Join-Path $PackageDir 'LICENSE.txt')
Copy-Item (Join-Path $ScriptDir 'THIRD-PARTY-NOTICES.md') $PackageDir
Copy-Item -Recurse (Join-Path $ScriptDir 'licenses') $PackageDir
Copy-Item (Join-Path $FFmpegDir 'LICENSE.txt') (Join-Path $PackageDir 'licenses\FFmpeg.txt')

# ── package ──────────────────────────────────────────────────────────────────

Log 'Packaging zip...'
$Zip = Join-Path $DistDir "$PackageName.zip"
Remove-Item -Force -ErrorAction SilentlyContinue $Zip
Compress-Archive -Path $PackageDir -DestinationPath $Zip

Log "Done: $Zip"
