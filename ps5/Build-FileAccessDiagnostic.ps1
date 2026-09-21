[CmdletBinding()]
param(
    [ValidatePattern('^[A-Z]{4}[0-9]{5}$')][string]$TitleId = 'PPSA99504',
    [switch]$RepairExecute
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/file-access/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$flags = @('--no-default-config', '--target=x86_64-unknown-freebsd', '-ffreestanding',
    '-fno-stack-protector', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
    ('-DMKW_DIAGNOSTIC_TITLE="' + $TitleId + '"'))
if ($RepairExecute) { $flags += '-DMKW_REPAIR_EXECUTE' }
& $clang @flags -c "$PSScriptRoot/diagnostics/file_access.c" -o "$out/diagnostic.o"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic compilation failed' }
# elfldr supplies a valid return continuation. The default payload exit path
# faulted after completing this diagnostic; use the explicit returning CRT.
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/diagnostic.o" --self-contained --kind payload --return-on-exit --out "$out/diagnostic.elf"
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic link failed' }
@{titleId=$TitleId;repairExecute=$RepairExecute.IsPresent;payload="$out/diagnostic.elf";
    sha256=(Get-FileHash "$out/diagnostic.elf").Hash;sent=$false} |
    ConvertTo-Json | Set-Content "$out/build.json"
Write-Host "Diagnostic built, not sent: $out/diagnostic.elf"
