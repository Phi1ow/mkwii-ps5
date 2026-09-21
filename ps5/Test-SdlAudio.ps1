[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/sdl-audio-host"
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
New-Item -ItemType Directory -Force $out | Out-Null
& $cmake -S "$PSScriptRoot/sdl" -B $out -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$llvm/clang.exe" "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" -DCMAKE_BUILD_TYPE=Release -DMKW_PS5_NATIVE_AUDIO=ON -DMKW_BUILD_AUDIO_TEST=ON *> "$out/configure.log"
if($LASTEXITCODE -ne 0){throw 'Host native audio configure failed'}
& $cmake --build $out --target audio_backend_test -j 4 *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Host native audio compile failed'}
$saved=$env:PATH
try{
    $env:PATH="$llvm;$saved"
    & "$out/audio_backend_test.exe" 2>&1 | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Audio contract test failed'}
}finally{$env:PATH=$saved}
