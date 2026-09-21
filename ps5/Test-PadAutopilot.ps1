[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/pad-autopilot"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/input" `
    "$PSScriptRoot/input/pad_autopilot.cpp" "$PSScriptRoot/tests/pad_autopilot.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'Pad autopilot test compile failed; see artifacts/pad-autopilot/host-build.log'}
$savedPath=$env:PATH
try{
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" 2>&1 | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Pad autopilot checks failed'}
}finally{$env:PATH=$savedPath}
