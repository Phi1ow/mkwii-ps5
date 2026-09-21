[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gpu-blit"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/blit_vertices.cpp" "$PSScriptRoot/tests/blit_vertices.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Blit host compile failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/test.exe" 2>&1 | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Blit geometry failed'}}finally{$env:PATH=$savedPath}
$sources=@('blit_vertices','agc_blit','gpu_fence','gpu_wait_service','color_target','gpu_color_target','texture_layout','depth_layout','depth_target','gpu_depth_target') | ForEach-Object { "$PSScriptRoot/gpu/$_.cpp" }
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" @sources "$PSScriptRoot/tests/agc_blit.cpp" -o "$out/lifetime-test.exe" *> "$out/lifetime-build.log"
if($LASTEXITCODE -ne 0){throw 'Blit lifetime compile failed'}
try{$env:PATH="$llvm;$savedPath";& "$out/lifetime-test.exe" 2>&1 | Tee-Object "$out/lifetime-host.log";if($LASTEXITCODE -ne 0){throw 'Blit lifetime test failed'}}finally{$env:PATH=$savedPath}
