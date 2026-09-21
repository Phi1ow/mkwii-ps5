[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-memory-sources"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$aurora="$root/Wiicompiled/aurora-main"
$deps="$root/.tools/game-deps"
$includes=@("$root/Wiicompiled/runtime/include","$PSScriptRoot/runtime","$PSScriptRoot/gpu","$aurora/lib","$aurora/include","$deps/fmt-11.1.4/include","$deps/xxHash-0.8.3","$deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public")|ForEach-Object{"-I$_"}
$sources=@("$PSScriptRoot/tests/guest_flat_ps5_test.cpp","$PSScriptRoot/tests/gx_memory_sources.cpp","$PSScriptRoot/runtime/guest_memory_ps5.cpp","$PSScriptRoot/runtime/guest_flat_memory_ps5.cpp","$PSScriptRoot/runtime/gx_host_memory_ps5.cpp","$root/Wiicompiled/runtime/src/memory.cpp","$root/Wiicompiled/runtime/src/ppc_memory_helpers.cpp","$PSScriptRoot/gpu/gx_memory_sources.cpp","$PSScriptRoot/gpu/gx_texture_cache.cpp","$PSScriptRoot/gpu/texture_layout.cpp","$aurora/lib/dolphin/gx/GXTexture.cpp")
& "$llvm/clang.exe" -O2 -c "$deps/xxHash-0.8.3/xxhash.c" -o "$out/xxhash.o" *> "$out/hash-build.log"
if($LASTEXITCODE -ne 0){throw 'Host xxHash compilation failed'}
$sources += @('dolphin/gx/GXExtra.cpp','dolphin/gx/frontend.cpp','gx/fifo.cpp','gx/register_decoder.cpp','gx/register_dispatch.cpp','gfx/texture_convert.cpp','logging.cpp')|ForEach-Object{"$aurora/lib/$_"}
$sources += @('gx_copy_texture_cache.cpp','gpu_color_target.cpp','color_target.cpp','gx_register_state.cpp','gpu_texture.cpp')|ForEach-Object{"$PSScriptRoot/gpu/$_"}
$sources += "$PSScriptRoot/tests/gx_memory_boundaries.cpp", "$deps/fmt-11.1.4/src/format.cc"
& "$llvm/clang++.exe" -std=c++20 -O2 -march=x86-64-v3 -DMKW_PLATFORM_PS5=1 -DMKW_GX_MEMORY_SOURCES_TEST=1 -DTARGET_PC=1 -DFMT_USE_FALLBACK_FILE=1 -DFMT_USE_FCNTL=0 -include cstdlib @includes @sources "$out/xxhash.o" -ldbghelp -luser32 -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'GX memory source host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/test.exe" 2>&1 | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'GX memory source tests failed'}}finally{$env:PATH=$saved}

