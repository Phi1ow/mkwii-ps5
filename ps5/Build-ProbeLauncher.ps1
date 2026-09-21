[CmdletBinding()]
param([Parameter(Mandatory)][ValidatePattern('^(PPSA99[56][0-9]{2}|WIKT0000[1-3])$')][string]$TitleId, [switch]$Close)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/probe-launcher/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$define = '-DMKW_DIAGNOSTIC_TITLE="' + $TitleId + '"'
$modeFlags = @()
if ($Close) { $modeFlags += '-DMKW_CLOSE_PROBE' }
& $clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -fPIC -fno-stack-protector -O2 -Wall -Wextra -Werror $define @modeFlags -c "$PSScriptRoot/diagnostics/launch_probe.c" -o "$out/launcher.o"
if ($LASTEXITCODE -ne 0) { throw 'Launcher compile failed' }
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/launcher.o" --self-contained --kind payload --return-on-exit --sprx libSceSystemService.sprx --sprx libSceUserService.sprx --out "$out/launcher.elf"
if ($LASTEXITCODE -ne 0) { throw 'Launcher link failed' }
Write-Host "Launcher built for $TitleId, not sent: $out/launcher.elf"
