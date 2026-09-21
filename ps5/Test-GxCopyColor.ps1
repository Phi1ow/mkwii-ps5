[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gx-copy-shader"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror -fno-fast-math -ffp-contract=off "-I$PSScriptRoot/gpu" "$PSScriptRoot/tests/gx_copy_color.cpp" -o "$out/color-test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'GX color-copy host compile failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/color-test.exe" 2>&1 | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'GX color-copy reference failed'}}finally{$env:PATH=$savedPath}
