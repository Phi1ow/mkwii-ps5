[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out="$root/artifacts/gx-lighting"
New-Item -ItemType Directory -Force $out | Out-Null
$aurora="$root/Wiicompiled/aurora-main"
& "$llvm/clang++.exe" -std=c++20 -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -DTARGET_PC=1 -DMKW_PLATFORM_PS5=1 -include cstdlib "-I$PSScriptRoot/gpu" "-I$aurora/include" "-I$aurora/lib" "-I$root/.tools/game-deps/fmt-11.1.4/include" "$PSScriptRoot/gpu/gx_lighting_state.cpp" "$PSScriptRoot/tests/gx_lighting.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Lighting compilation failed; see host-build.log'}
$savedPath=$env:PATH
try {
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'GX lighting tests failed'}
}finally{$env:PATH=$savedPath}
