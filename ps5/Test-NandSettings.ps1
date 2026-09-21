[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/nand-settings/host-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$env:PATH="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin;$env:PATH"
& "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang++.exe" -std=c++17 -O2 -Wall -Wextra -Werror -DMKW_PLATFORM_PS5 "-I$root/Wiicompiled/runtime/include" "$PSScriptRoot/tests/nand_settings.cpp" -o "$out/test.exe"
if($LASTEXITCODE -ne 0){throw 'NAND host contract compilation failed'}
& "$out/test.exe" "$out/cases" *> "$out/test.log"
if($LASTEXITCODE -ne 0){throw "NAND contract failed: $out/test.log"}
Get-Content "$out/test.log"
