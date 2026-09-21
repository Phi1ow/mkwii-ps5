[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/frame-timing"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "-I$root/Wiicompiled/aurora-main/include" "$PSScriptRoot/gpu/present_history.cpp" "$PSScriptRoot/gpu/gpu_wait_service.cpp" "$PSScriptRoot/tests/frame_timing.cpp" -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Frame timing host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Frame timing test failed'}}finally{$env:PATH=$saved}
