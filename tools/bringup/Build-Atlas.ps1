<#
.SYNOPSIS
Builds and verifies the inhibited Atlas bench firmware, without accessing hardware.
.PARAMETER Python
Optional Python executable; defaults to this project's virtual environment.
#>
param([string]$Python = '')
$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$atlasCompiler = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
if (-not $atlasCompiler) {
    $atlasCompiler = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'stm32cube/bundles/gnu-tools-for-stm32/*/bin/arm-none-eabi-gcc.exe') -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
}
if (-not $atlasCompiler) { throw 'Install the STM32 Arm GNU toolchain or add it to PATH.' }
$atlasCompilerPath = if ($atlasCompiler.Source) { $atlasCompiler.Source } else { $atlasCompiler.FullName }
$env:PATH = (Split-Path -Parent $atlasCompilerPath) + ';' + $env:PATH
if (-not $Python) { $Python = Join-Path $atlasRoot '.venv/Scripts/python.exe' }
if (-not (Test-Path -LiteralPath $Python)) { throw 'Create .venv and install tools/bringup/requirements.txt first.' }
$atlasMake = Get-Command gmake -ErrorAction SilentlyContinue
if (-not $atlasMake) { throw 'This Windows build script uses GNU Make (gmake); the CMake presets also support Ninja.' }
Push-Location $atlasRoot
try {
    & cmake -S . -B build/BenchMake -G 'MinGW Makefiles' '-DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake' '-DCMAKE_BUILD_TYPE=Debug' '-DATLAS_BRINGUP=ON' "-DCMAKE_MAKE_PROGRAM=$($atlasMake.Source)"
    if ($LASTEXITCODE -ne 0) { throw 'Firmware configuration failed.' }
    & cmake --build build/BenchMake --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Firmware build failed.' }
    & $Python tools/bringup/image_check.py build/BenchMake/Atlas-Bringup.manifest.json
    if ($LASTEXITCODE -ne 0) { throw 'Firmware image verification failed.' }
} finally { Pop-Location }
