[CmdletBinding()]
param([ValidateRange(-1,2147483647)][int]$ProcessId=-1)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/kernel-exports/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out|Out-Null
$clang="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe"
& $clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -fno-stack-protector -fPIC -O2 -Wall -Wextra -Werror "-DMKW_EXPORT_PID=$ProcessId" -c "$PSScriptRoot/diagnostics/kernel_exports.c" -o "$out/diagnostic.o"
if($LASTEXITCODE -ne 0){throw 'Export diagnostic compile failed'}
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/diagnostic.o" --self-contained --kind payload --return-on-exit --out "$out/diagnostic.elf"
if($LASTEXITCODE -ne 0){throw 'Export diagnostic link failed'}
Copy-Item -LiteralPath "$PSScriptRoot/diagnostics/kernel_exports.c" -Destination "$out/source.c"
@{payload="$out/diagnostic.elf";processId=$ProcessId;sha256=(Get-FileHash "$out/diagnostic.elf").Hash;sent=$false}|ConvertTo-Json|Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Read-only export diagnostic built: $out/diagnostic.elf"
