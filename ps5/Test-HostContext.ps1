[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/host-context-tests"
New-Item -ItemType Directory -Force $out | Out-Null
$include = "-I$root/Wiicompiled/runtime/include"
& $clang -std=c++17 -O2 -Wall -Wextra -Werror $include "$PSScriptRoot/tests/host_context_ps5_test.cpp" "$PSScriptRoot/runtime/host_context_ps5.cpp" "$PSScriptRoot/runtime/context_x86_64.S" -o "$out/ps5-backend.exe"
if ($LASTEXITCODE -ne 0) { throw 'PS5 backend host harness compile failed' }
& "$out/ps5-backend.exe"
if ($LASTEXITCODE -ne 0) { throw 'PS5 backend host harness failed' }
& $clang -std=c++17 -O2 $include "$root/Wiicompiled/runtime/tests/host_context_tests.cpp" "$root/Wiicompiled/runtime/src/host_context.cpp" -o "$out/windows-backend.exe"
if ($LASTEXITCODE -ne 0) { throw 'Upstream Windows context compile failed' }
& "$out/windows-backend.exe"
if ($LASTEXITCODE -ne 0) { throw 'Upstream Windows context test failed' }
& $clang --no-default-config --target=x86_64-unknown-freebsd -std=c++17 -ffreestanding -fPIC -fno-exceptions -fno-rtti -O2 -Wall -Wextra -Werror $include -c "$PSScriptRoot/runtime/host_context_ps5.cpp" -o "$out/host-context-ps5.o"
if ($LASTEXITCODE -ne 0) { throw 'Native PS5 context compile failed' }
Write-Host 'PS5 backend passes host checks and compiles to ELF. Native console validation remains pending.'
