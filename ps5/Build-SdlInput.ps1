[CmdletBinding()]
param(
    [string]$CMake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    [string]$Ninja='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/sdl-input"
New-Item -ItemType Directory -Force $out|Out-Null
@{compiled=$false;nativeExecution=$false}|ConvertTo-Json|Set-Content "$out/result.json" -Encoding utf8NoBOM
& $CMake -S "$PSScriptRoot/sdl" -B $out -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/ps5-cxx.cmake" -DCMAKE_BUILD_TYPE=Release *> "$out/configure.log"
if($LASTEXITCODE -ne 0){throw 'SDL input configure failed'}
& $CMake --build $out --target SDL3-static -j 4 *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'SDL input build failed'}
$archive=Get-Item "$out/sdl/libSDL3.a"
@{compiled=$true;nativeExecution=$false;archive=$archive.FullName;sha256=(Get-FileHash $archive.FullName).Hash;video=$false;audio=$false;filesystemOperations=$true;platformPathDiscovery=$false;virtualJoystick=$true}|ConvertTo-Json|Set-Content "$out/result.json" -Encoding utf8NoBOM
Write-Host 'SDL input/events archive compiled; native integration still requires validation.'
