[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-fog"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$aurora="$root/Wiicompiled/aurora-main"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -DTARGET_PC=1 -DMKW_PLATFORM_PS5=1 -include cstdlib `
    "-I$PSScriptRoot/gpu" "-I$aurora/include" "-I$aurora/lib" "-I$root/.tools/game-deps/fmt-11.1.4/include" `
    "$PSScriptRoot/gpu/gx_fog_state.cpp" "$PSScriptRoot/tests/gx_fog.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw "GX fog test compile failed; see artifacts/gx-fog/host-build.log"}
$savedPath=$env:PATH
try{
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" 2>&1 | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'GX fog checks failed'}
}finally{$env:PATH=$savedPath}
