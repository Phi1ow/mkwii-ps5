[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/native-input"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/input" "$PSScriptRoot/input/native_pad.cpp" "$PSScriptRoot/input/pad_sample.cpp" "$PSScriptRoot/tests/native_pad.cpp" -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Native input host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Native input test failed'}}finally{$env:PATH=$saved}
