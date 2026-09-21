[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out="$root/artifacts/gx-copy-store"
New-Item -ItemType Directory -Force $out | Out-Null
$aurora="$root/Wiicompiled/aurora-main"
$includes=@("$root/Wiicompiled/runtime/include","$PSScriptRoot/runtime","$PSScriptRoot/gpu","$aurora/lib","$aurora/include","$root/.tools/game-deps/fmt-11.1.4/include")|ForEach-Object{"-I$_"}
$sources=@('gx_copy_store.cpp','gx_copy_deferred.cpp','gx_copy_readback.cpp','gx_copy_texture_cache.cpp','gx_register_state.cpp','gpu_color_target.cpp','color_target.cpp','texture_layout.cpp')|ForEach-Object{"$PSScriptRoot/gpu/$_"}
$sources+=@("$PSScriptRoot/tests/guest_flat_ps5_test.cpp","$PSScriptRoot/tests/gx_copy_store.cpp","$PSScriptRoot/runtime/guest_flat_memory_ps5.cpp","$PSScriptRoot/runtime/guest_memory_ps5.cpp","$root/Wiicompiled/runtime/src/memory.cpp","$aurora/lib/gfx/efb_ram_encoder.cpp")
$sources+="$root/Wiicompiled/runtime/src/ppc_memory_helpers.cpp"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DMKW_PLATFORM_PS5=1 -DMKW_GPU_COPY_STORE_TEST=1 -DTARGET_PC=1 -include cstdlib @includes @sources -ldbghelp -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Copy-store build failed; see artifacts/gx-copy-store/build.log'}
$savedPath=$env:PATH
try{
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" 2>&1 | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Copy-store test failed'}
}finally{$env:PATH=$savedPath}
