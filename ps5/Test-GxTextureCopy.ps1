[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-texture-copy"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$includes=@("$PSScriptRoot/gpu","$root/Wiicompiled/aurora-main/include","$root/Wiicompiled/aurora-main/lib","$root/.tools/game-deps/fmt-11.1.4/include")|ForEach-Object{"-I$_"}
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -include cstdlib @includes "$PSScriptRoot/gpu/gx_texture_copy_plan.cpp" "$PSScriptRoot/gpu/blit_vertices.cpp" "$PSScriptRoot/tests/gx_texture_copy_plan.cpp" -o "$out/plan-test.exe" *> "$out/plan-build.log"
if($LASTEXITCODE -ne 0){throw 'GX texture-copy planner build failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/plan-test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'GX texture-copy planner failed'}}finally{$env:PATH=$savedPath}
