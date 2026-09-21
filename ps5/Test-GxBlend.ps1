[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-blend"
New-Item -ItemType Directory -Force $out | Out-Null
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
$env:NUGET_PACKAGES="$root/.tools/nuget"
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/blend_fixture" -c Release -- "$out/reference.bin" *> "$out/reference-build.log"
if($LASTEXITCODE -ne 0){throw 'Blend reference build failed'}
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$aurora="$root/Wiicompiled/aurora-main"
& "$llvm/clang++.exe" -std=c++20 -O2 -DTARGET_PC=1 -DMKW_PLATFORM_PS5=1 -include cstdlib "-I$PSScriptRoot/gpu" "-I$aurora/include" "-I$aurora/lib" "-I$root/.tools/game-deps/fmt-11.1.4/include" "$PSScriptRoot/gpu/gx_blend_state.cpp" "$PSScriptRoot/tests/gx_blend.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Blend test compile failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/test.exe" "$out/reference.bin" | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Blend tests failed'}}finally{$env:PATH=$savedPath}
