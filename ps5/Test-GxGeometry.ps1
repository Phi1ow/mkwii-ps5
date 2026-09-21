[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out="$root/artifacts/gx-geometry"
New-Item -ItemType Directory -Force $out | Out-Null
$aurora="$root/Wiicompiled/aurora-main"
$fmt="$root/.tools/game-deps/fmt-11.1.4"
$tracy="$root/.tools/game-deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public"
& "$llvm/clang.exe" -std=c17 -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -DMKW_VERTEX_HOST_TEST -DMKW_GX_TRANSFORMS -c "$PSScriptRoot/gpu/shaders/gx_vertex_raw.c" -o "$out/vertex_host.o" *> "$out/shader-build.log"
if($LASTEXITCODE -ne 0){throw 'Host shader compilation failed; see shader-build.log'}
$sources=@('dolphin/gx/frontend.cpp','gx/fifo.cpp','gx/register_decoder.cpp','gx/register_dispatch.cpp','logging.cpp') | ForEach-Object {"$aurora/lib/$_"}
$sources+=@('gx_geometry.cpp','gx_geometry_buffer.cpp','gx_transform_state.cpp','gx_draw.cpp','gx_register_state.cpp') | ForEach-Object {"$PSScriptRoot/gpu/$_"}
& "$llvm/clang++.exe" -std=c++20 -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -DNDEBUG -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -DFMT_USE_FALLBACK_FILE=1 -DFMT_USE_FCNTL=0 -include cstdlib "-I$PSScriptRoot/gpu" "-I$aurora/include" "-I$aurora/lib" "-I$fmt/include" "-I$tracy" @sources "$fmt/src/format.cc" "$PSScriptRoot/tests/gx_geometry.cpp" "$out/vertex_host.o" -luser32 -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'GX geometry compilation failed; see artifacts/gx-geometry/build.log'}
$savedPath=$env:PATH
try {
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'GX geometry tests failed'}
}finally{$env:PATH=$savedPath}
