[CmdletBinding()]
param(
    # Title identifiers whose home-screen registration is removed.
    [Parameter(Mandatory)][string[]]$Titles,
    # PPSA99611 holds the game DATA and NAND: only its registration may be
    # removed, to let ShadowMount register it again with new metadata.
    [switch]$AllowGameTitle,
    # Only report whether each title is registered.
    [switch]$CheckOnly
)
# Builds (does not send) ps5/diagnostics/app_uninstall.c for project test titles.
# AppInstUtil needs libSceIpmi loaded as well (SharpProspero PayloadAppInstaller);
# without it sceAppInstUtilInitialize never returned (PORTAGE.md §83).
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$Titles = @($Titles | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($title in $Titles) {
    if ($title -cnotmatch '^(PPSA99[56][0-9]{2}|WIKT0000[0-9])$') { throw "Not a project test title: $title" }
    if ($title -eq 'PPSA99611' -and !$AllowGameTitle -and !$CheckOnly) { throw 'PPSA99611 is the installed game; pass -AllowGameTitle to re-register it' }
}
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/title-uninstall/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force $out | Out-Null
$list = @('-DMKW_UNINSTALL_TITLES="' + ($Titles -join ',') + '"')
if ($CheckOnly) { $list += '-DMKW_CHECK_ONLY' }
& $clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -fPIC -fno-stack-protector -O2 -Wall -Wextra -Werror @list -c "$PSScriptRoot/diagnostics/app_uninstall.c" -o "$out/uninstall.o"
if ($LASTEXITCODE -ne 0) { throw 'Uninstall payload compile failed' }
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/uninstall.o" --self-contained --kind payload --return-on-exit --sprx libSceIpmi.sprx --sprx libSceAppInstUtil.sprx --out "$out/uninstall.elf"
if ($LASTEXITCODE -ne 0) { throw 'Uninstall payload link failed' }
@{titles = $Titles; checkOnly = $CheckOnly.IsPresent; payload = "$out/uninstall.elf"; sha256 = (Get-FileHash "$out/uninstall.elf").Hash; sent = $false} |
    ConvertTo-Json | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Uninstall payload built, not sent: $out/uninstall.elf"
