[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$llvm = "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out = "$root/artifacts/gx-textures"
New-Item -ItemType Directory -Force $out | Out-Null
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/sampler_fixture/sampler_fixture.csproj" -c Release -- "$out/samplers.bin" *> "$out/sampler-build.log"
if ($LASTEXITCODE -ne 0) { throw 'SharpProspero sampler fixture build failed; see artifacts/gx-textures/sampler-build.log' }
$aurora = "$root/Wiicompiled/aurora-main"
$deps = "$root/.tools/game-deps"
$includes = @("$PSScriptRoot/gpu", "$aurora/include", "$aurora/lib", "$deps/fmt-11.1.4/include", "$deps/xxHash-0.8.3", "$deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public") | ForEach-Object { "-I$_" }
& "$llvm/clang.exe" -O2 -c "$deps/xxHash-0.8.3/xxhash.c" -o "$out/xxhash.o" *> "$out/hash-build.log"
if ($LASTEXITCODE -ne 0) { throw 'Host xxHash compilation failed' }
$sources = @('dolphin/gx/GXTexture.cpp','dolphin/gx/GXExtra.cpp','dolphin/gx/frontend.cpp','gx/fifo.cpp','gx/register_decoder.cpp','gx/register_dispatch.cpp','gfx/texture_convert.cpp','gfx/efb_ram_encoder.cpp','logging.cpp') | ForEach-Object { "$aurora/lib/$_" }
$sources += @('gx_texture_cache.cpp','gx_copy_texture_cache.cpp','color_target.cpp','gpu_color_target.cpp','gx_register_state.cpp','gpu_texture.cpp','texture_layout.cpp','gx_sampler.cpp','gx_tev_program.cpp','gx_fog_state.cpp','gx_direct_material.cpp') | ForEach-Object { "$PSScriptRoot/gpu/$_" }
$sources += "$PSScriptRoot/tests/gx_direct_material.cpp"
$sources += "$PSScriptRoot/gpu/gx_copy_readback.cpp", "$PSScriptRoot/tests/gx_copy_readback.cpp"
& "$llvm/clang++.exe" -std=c++20 -O2 -march=x86-64-v3 -fno-fast-math -ffp-contract=off -DNDEBUG -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -DFMT_USE_FALLBACK_FILE=1 -DFMT_USE_FCNTL=0 -include cstdlib @includes @sources "$deps/fmt-11.1.4/src/format.cc" "$PSScriptRoot/tests/gx_texture_cache.cpp" "$out/xxhash.o" -luser32 -o "$out/test.exe" *> "$out/build.log"
if ($LASTEXITCODE -ne 0) { throw 'GX texture cache compile failed; see artifacts/gx-textures/build.log' }
$savedPath = $env:PATH
try {
    $env:PATH = "$llvm;$savedPath"
    & "$out/test.exe" "$out/samplers.bin" 2>&1 | Tee-Object "$out/host.log"
    if ($LASTEXITCODE -ne 0) { throw 'GX texture cache checks failed' }
} finally { $env:PATH = $savedPath }
