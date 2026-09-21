[CmdletBinding()]
param(
    [ValidatePattern('^[A-Z]{4}[0-9]{5}$')][string]$TitleId = 'PPSA99611',
    [Parameter(Mandatory)][ValidatePattern('^[a-z_]+_[0-9]+_pid[0-9]+$')][string]$Run
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/log-access/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$flags = @('--no-default-config', '--target=x86_64-unknown-freebsd', '-ffreestanding',
    '-fno-stack-protector', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror',
    ('-DMKW_DIAGNOSTIC_TITLE="' + $TitleId + '"'), ('-DMKW_LOG_RUN="' + $Run + '"'))
& $clang @flags -c "$PSScriptRoot/diagnostics/log_access.c" -o "$out/diagnostic.o"
if ($LASTEXITCODE -ne 0) { throw 'Log access diagnostic compilation failed' }
# Same returning payload CRT as Build-FileAccessDiagnostic.ps1.
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/diagnostic.o" --self-contained --kind payload --return-on-exit --out "$out/diagnostic.elf"
if ($LASTEXITCODE -ne 0) { throw 'Log access diagnostic link failed' }
@{titleId=$TitleId;run=$Run;payload="$out/diagnostic.elf";sha256=(Get-FileHash "$out/diagnostic.elf").Hash;sent=$false} |
    ConvertTo-Json | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Log access diagnostic built, not sent: $out/diagnostic.elf"
