[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
$out="$root/artifacts/sdl-audio"
New-Item -ItemType Directory -Force $out | Out-Null
@{compiled=$false;nativeExecution=$false} | ConvertTo-Json | Set-Content "$out/result.json" -Encoding utf8NoBOM
& $cmake -S "$PSScriptRoot/sdl" -B $out -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/ps5-cxx.cmake" -DCMAKE_BUILD_TYPE=Release -DMKW_PS5_NATIVE_AUDIO=ON *> "$out/configure.log"
if($LASTEXITCODE -ne 0){throw 'SDL audio configure failed'}
& $cmake --build $out --target SDL3-static -j 4 *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'SDL audio build failed'}
$archive="$out/sdl/libSDL3.a"
@{compiled=$true;nativeExecution=$false;archive=$archive;sha256=(Get-FileHash $archive).Hash;
    audio=$true;driver='ps5';outputRate=48000;outputChannels=2;format='S16LE';grain=1024;
    video=$false;filesystemOperations=$true;virtualJoystick=$true} |
    ConvertTo-Json | Set-Content "$out/result.json" -Encoding utf8NoBOM
Write-Host 'Native SDL AudioOut driver compiled separately; not yet selected by the game build.'
