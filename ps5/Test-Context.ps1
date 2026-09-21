[CmdletBinding()]
param([string]$Clang)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Clang) { $Clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName }
$out = "$root/artifacts/context-tests"
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($optimization in @('O0', 'O2')) {
    & $Clang "-$optimization" -Wall -Wextra -Werror "$PSScriptRoot/tests/context_test.c" "$PSScriptRoot/tests/context_registers.S" "$PSScriptRoot/runtime/context_x86_64.S" -o "$out/context-$optimization.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Context harness compilation failed' }
    & "$out/context-$optimization.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Context ABI verification failed' }
}
& $Clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -c "$PSScriptRoot/runtime/context_x86_64.S" -o "$out/context-ps5.o"
if ($LASTEXITCODE -ne 0) { throw 'Context ELF assembly failed' }
Write-Host 'Host ABI checks passed; console execution and HostContext integration remain pending.'
