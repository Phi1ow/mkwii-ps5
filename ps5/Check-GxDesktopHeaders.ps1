[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/artifacts/gx-registers"
$aurora = "$root/Wiicompiled/aurora-main"
$deps = "$root/.tools/game-deps"
$check = "$root/.tools/desktop-check"
if (!(Test-Path "$check/dawn/include/webgpu/webgpu_cpp.h")) { throw 'Run Prepare-DesktopCheck.ps1 first' }
$includes = @("$aurora/include", "$aurora/lib", "$check/dawn/include", "$check/abseil-cpp-20240722.0", "$deps/fmt-11.1.4/include", "$deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public", "$deps/xxHash-0.8.3", "$deps/SDL3-3.4.4/include", "$deps/imgui-1.91.9b-docking") | ForEach-Object { "-I$_" }
$sources = @('gx/gx.cpp','gx/command_processor.cpp','gx/register_decoder.cpp','gx/register_dispatch.cpp','gx/shader_info.cpp','gx/shader.cpp','gx/pipeline.cpp','gfx/texture_convert.cpp','dolphin/gx/GXGeometry.cpp','dolphin/gx/GXTransform.cpp','dolphin/gx/GXGet.cpp','dolphin/gx/GXExtra.cpp','dolphin/gx/GXTexture.cpp') | ForEach-Object { "$aurora/lib/$_" }
$sources += "$aurora/tests/gx_test_stubs.cpp"
# Desktop macro path and real pinned Dawn/Abseil headers; no GPU library linked.
& "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe" -fsyntax-only -std=c++20 -DNDEBUG -DTARGET_PC=1 -DAURORA -include cstdlib @includes @sources *> "$out/desktop-syntax.log"
if ($LASTEXITCODE -ne 0) { throw 'Shared desktop sources no longer compile; see artifacts/gx-registers/desktop-syntax.log' }
Write-Host "PASS desktop syntax: $($sources.Count) affected/dependent files with real Dawn headers; no desktop link or GPU run"
