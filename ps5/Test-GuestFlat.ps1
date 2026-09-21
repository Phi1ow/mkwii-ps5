[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/guest-flat-tests"
New-Item -ItemType Directory -Force $out | Out-Null
& $clang -std=c++17 -O2 -Wall -Wextra -Werror -Wno-unused-parameter -DMKW_PLATFORM_PS5=1 "-I$root/Wiicompiled/runtime/include" "-I$PSScriptRoot/runtime" "$PSScriptRoot/tests/guest_flat_ps5_test.cpp" "$PSScriptRoot/runtime/guest_flat_memory_ps5.cpp" "$PSScriptRoot/runtime/guest_memory_ps5.cpp" "$root/Wiicompiled/runtime/src/memory.cpp" "$root/Wiicompiled/runtime/src/ppc_memory_helpers.cpp" -ldbghelp -o "$out/guest-flat.exe"
if ($LASTEXITCODE -ne 0) { throw 'GuestFlat integration test compile failed' }
$previousPath = $env:PATH
try {
    $env:PATH = "$(Split-Path $clang);$previousPath"
    & "$out/guest-flat.exe"
    if ($LASTEXITCODE -ne 0) { throw "GuestFlat integration test failed: $LASTEXITCODE" }
} finally { $env:PATH = $previousPath }

