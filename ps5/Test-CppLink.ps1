[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/cpp-link/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
& $clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -fPIC -O2 -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -c "$PSScriptRoot/tests/cpp_runtime_smoke.cpp" -o "$out/runtime.o"
if ($LASTEXITCODE -ne 0) { throw 'C++ smoke compilation failed' }
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/runtime.o" --self-contained --kind eboot --out "$out/runtime.elf"
if ($LASTEXITCODE -ne 0) { throw 'C++ smoke link failed' }
& (Join-Path (Split-Path $clang) 'llvm-readelf.exe') -l -d "$out/runtime.elf" | Set-Content "$out/elf-layout.txt"
if ($LASTEXITCODE -ne 0) { throw 'ELF inspection failed' }
Get-Content "$out/elf-layout.txt" | Select-String 'TLS|INIT_ARRAY'
Write-Host "C++ link produced $out/runtime.elf. Constructors and TLS need execution tests; no C++ standard library is linked."
