[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-frame-renderer"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$includes=@("-I$PSScriptRoot/gpu")
# Keep project warnings strict without promoting warnings in supplied headers.
foreach($dir in @("$root/Wiicompiled/aurora-main/include","$root/Wiicompiled/aurora-main/lib","$root/.tools/game-deps/fmt-11.1.4/include")){$includes+=@('-isystem',$dir)}
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -include cstdlib @includes "$PSScriptRoot/gpu/gx_video_state.cpp" "$PSScriptRoot/tests/gx_video_state.cpp" -o "$out/plan-test.exe" *> "$out/plan-build.log"
if($LASTEXITCODE -ne 0){throw 'VI render-plan host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/plan-test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'VI render-plan test failed'}}finally{$env:PATH=$saved}
