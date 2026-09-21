[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/memory-native/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$flags = @('--no-default-config','--target=x86_64-unknown-freebsd','-ffreestanding','-fPIC',
    '-fno-stack-protector','-O2','-Wall','-Wextra','-Werror','-std=c++17','-fno-exceptions','-fno-rtti',"-I$PSScriptRoot/runtime")
& $clang @flags -c "$PSScriptRoot/diagnostics/guest_memory.cpp" -o "$out/diagnostic.o"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic compile failed' }
& $clang @flags -c "$PSScriptRoot/runtime/guest_memory_ps5.cpp" -o "$out/backend.o"
if ($LASTEXITCODE -ne 0) { throw 'Memory backend compile failed' }
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/diagnostic.o" --obj "$out/backend.o" --self-contained --kind payload --return-on-exit --out "$out/diagnostic.elf"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic link failed' }
Write-Host "Built PS5 guest-memory diagnostic: $out/diagnostic.elf"
