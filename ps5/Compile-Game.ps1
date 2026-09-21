[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    [string]$Ninja = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe',
    # DIAGNOSTIC: build the exact function profiler variant in artifacts/game-build-fprof.
    [switch]$FunctionProfile,
    # Retro Rewind product: links the mod shards and its dispatch profile
    # (requires Translate-Game.ps1 -RetroRewind). Builds in artifacts/game-build-retro.
    [switch]$RetroRewind
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$suffix = if ($FunctionProfile) { '-fprof' } else { '' }
if ($RetroRewind) { $suffix += '-retro' }
$build = "$root/artifacts/game-build$suffix"
& $CMake -S "$PSScriptRoot" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/ps5-cxx.cmake" '-DCMAKE_BUILD_TYPE=Release' '-DMKW_PS5_BUILD_CPP_ADAPTER=ON' '-DMKW_PS5_BUILD_GAME_OBJECTS=ON' "-DMKW_PS5_RETRO_REWIND=$(if ($RetroRewind) { 'ON' } else { 'OFF' })" "-DMKW_PS5_FUNCTION_PROFILE=$(if ($FunctionProfile) { 'ON' } else { 'OFF' })" *> "$root/artifacts/game-configure$suffix.log"
if ($LASTEXITCODE -ne 0) { throw "Game configure failed; see artifacts/game-configure$suffix.log" }
& $CMake --build $build -j 6 *> "$root/artifacts/game-build$suffix.log"
if ($LASTEXITCODE -ne 0) { throw "Game object compilation failed; see artifacts/game-build$suffix.log" }
@{product=$(if ($RetroRewind) { 'retro_rewind' } else { 'base' });translatedArchiveSha256=(Get-FileHash "$build/libmkw_ps5_translated.a").Hash;compiled=$true;linkedGame=$false;nativeExecution=$false} | ConvertTo-Json | Set-Content "$build/compile-result.json" -Encoding utf8NoBOM
Write-Host 'PS5 translated game and platform objects compiled. This is not yet a linked game.'
