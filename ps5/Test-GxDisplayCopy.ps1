[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-display-copy"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$includes=@("$PSScriptRoot/gpu","$root/Wiicompiled/aurora-main/include","$root/Wiicompiled/aurora-main/lib","$root/.tools/game-deps/fmt-11.1.4/include")|ForEach-Object{"-I$_"}
$sources=@('gx_display_copy_plan.cpp','gx_texture_copy_plan.cpp','blit_vertices.cpp')|ForEach-Object{"$PSScriptRoot/gpu/$_"}
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -include cstdlib @includes @sources "$PSScriptRoot/tests/gx_display_copy_plan.cpp" -o "$out/plan-test.exe" *> "$out/plan-build.log"
if($LASTEXITCODE -ne 0){throw 'Display-copy planner build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/plan-test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Display-copy planner failed'}}finally{$env:PATH=$saved}
