[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/memory-policy"
New-Item -ItemType Directory -Force $out | Out-Null
& $clang -std=c++17 -O2 -DMKW_PLATFORM_PS5=1 "-I$root/Wiicompiled/runtime/include" "$PSScriptRoot/tests/memory_policy_test.cpp" -o "$out/policy.exe"
if ($LASTEXITCODE -ne 0) { throw 'Memory policy test compile failed' }
& "$out/policy.exe"
if ($LASTEXITCODE -ne 0) { throw 'Memory policy test failed' }
