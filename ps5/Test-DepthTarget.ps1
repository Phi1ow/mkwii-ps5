[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/depth-target"
New-Item -ItemType Directory -Force $out | Out-Null
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
$env:NUGET_PACKAGES="$root/.tools/nuget"
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/depth_target_fixture" -c Release -- "$out/reference.bin" *> "$out/reference-build.log"
if($LASTEXITCODE -ne 0){throw 'Depth target reference build failed'}
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/depth_target.cpp" "$PSScriptRoot/gpu/gpu_depth_target.cpp" "$PSScriptRoot/gpu/depth_layout.cpp" "$PSScriptRoot/tests/depth_target.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Depth target test compile failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/test.exe" "$out/reference.bin" 2>&1 | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Depth target tests failed'}}finally{$env:PATH=$savedPath}
