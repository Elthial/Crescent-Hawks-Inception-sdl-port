param(
    [string]$BuildRoot = (Join-Path $PSScriptRoot '../out/release-ci')
)

$ErrorActionPreference = 'Stop'
$source = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
$platform = if ($IsWindows) { 'windows' } elseif ($IsLinux) { 'linux' } else { throw 'Windows or Linux required' }
$cmake = 'cmake'
$ctest = 'ctest'
if ($IsWindows) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $installation) { throw 'Visual Studio C/C++ tools required' }
    Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
}

& (Join-Path $source 'tools/Test-EvidenceLayout.ps1')
New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
# Hosted release builds never use local/proprietary game files.
$env:CHI_ASSET_DIRECTORY = ''
$env:CHI_ORIGINAL_ASSET_DIRECTORY = ''
$env:SDL_VIDEODRIVER = 'dummy'
$env:SDL_AUDIODRIVER = 'dummy'
foreach ($configuration in @('Release', 'Debug')) {
    foreach ($kind in @('headless', 'sdl')) {
        $build = Join-Path $BuildRoot "$kind-$($configuration.ToLowerInvariant())"
        $options = @('-S', $source, '-B', $build, '-G', 'Ninja',
            "-DCMAKE_BUILD_TYPE=$configuration", '-DBUILD_TESTING=ON',
            '-DCHI_ORIGINAL_ASSET_DIRECTORY=')
        if ($kind -eq 'headless') {
            $options += '-DCHI_BUILD_SDL=OFF'
        } else {
            # Always use the checksum-pinned, statically linked SDL source.
            $options += @('-DCHI_BUILD_SDL=ON', '-DCHI_FETCH_SDL=ON',
                '-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON')
        }
        if ($IsLinux) { $options += '-DCHI_WARNINGS_AS_ERRORS=OFF' }
        & $cmake @options
        if ($LASTEXITCODE -ne 0) { throw "$kind $configuration configure failed" }
        & $cmake --build $build --parallel 4
        if ($LASTEXITCODE -ne 0) { throw "$kind $configuration build failed" }
        $inventoryJson = & $ctest --test-dir $build --show-only=json-v1
        if ($LASTEXITCODE -ne 0) { throw 'CTest inventory failed' }
        $inventory = ($inventoryJson -join "`n") | ConvertFrom-Json
        $minimumTests = if ($kind -eq 'headless') { 117 } else { 148 }
        if ($inventory.tests.Count -lt $minimumTests) {
            throw "$kind configured only $($inventory.tests.Count) tests; expected at least $minimumTests"
        }
        & $ctest --test-dir $build --output-on-failure --no-tests=error --parallel 1 `
            --output-junit (Join-Path $BuildRoot "$kind-$configuration.xml")
        if ($LASTEXITCODE -ne 0) { throw "$kind $configuration tests failed" }
    }
}

# Package only after all four suites succeed.
$releaseBuild = Join-Path $BuildRoot 'sdl-release'
& $cmake --build $releaseBuild --target inception-package
if ($LASTEXITCODE -ne 0) { throw 'Runtime package build failed' }
& python (Join-Path $source 'tools/release_assets.py') package `
    --version ((Get-Content (Join-Path $source 'VERSION') -Raw).Trim()) `
    --platform $platform --directory (Join-Path $releaseBuild 'package') `
    --output (Join-Path $BuildRoot 'dist')
if ($LASTEXITCODE -ne 0) { throw 'Archive validation failed' }
