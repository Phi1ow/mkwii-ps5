[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/sdl-input-host"
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$cmake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ninja='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
New-Item -ItemType Directory -Force $out|Out-Null
& $cmake -S "$PSScriptRoot/sdl" -B $out -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_C_COMPILER=$llvm/clang.exe" "-DCMAKE_CXX_COMPILER=$llvm/clang++.exe" -DCMAKE_BUILD_TYPE=Release -DSDL_XINPUT=ON -DMKW_BUILD_VIRTUAL_PAD_TEST=ON *> "$out/configure.log"
if($LASTEXITCODE -ne 0){throw 'Host SDL configure failed'}
& $cmake --build $out --target virtual_pad_test aurora_pad_test aurora_input_test -j 4 *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Virtual pad host build failed'}
$saved=$env:PATH
try{
    $env:PATH="$llvm;$saved"
    & "$out/virtual_pad_test.exe" 2>&1|Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Virtual pad test failed'}
    $config="$out/test-config-$([Guid]::NewGuid().ToString('N'))"
    & "$out/aurora_pad_test.exe" $config 2>&1|Tee-Object "$out/aurora-host.log"
    if($LASTEXITCODE -ne 0){throw 'Aurora PAD integration test failed'}
    & "$out/aurora_pad_test.exe" $config 2>&1|Tee-Object "$out/aurora-reload-host.log"
    if($LASTEXITCODE -ne 0){throw 'Aurora PAD persistence reload failed'}
    & "$out/aurora_input_test.exe" "$out/loop-config-$([Guid]::NewGuid().ToString('N'))" 2>&1|Tee-Object "$out/aurora-loop-host.log"
    if($LASTEXITCODE -ne 0){throw 'Aurora input loop test failed'}
}finally{$env:PATH=$saved}
