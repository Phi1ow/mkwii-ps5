[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/fast-clock"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/runtime" "$PSScriptRoot/tests/fast_clock.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Fast clock test compile failed; see artifacts/fast-clock/host-build.log'}
$savedPath=$env:PATH
try{
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" 2>&1 | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Fast clock checks failed'}
}finally{$env:PATH=$savedPath}
