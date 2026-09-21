[CmdletBinding()]
param([string]$Module)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$Module) { $Module = "$root/.tools/console/libc.prx" }
$out = "$root/artifacts"
New-Item -ItemType Directory -Force $out | Out-Null
$exports = & "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" elf --file $Module --exports
if ($LASTEXITCODE -ne 0) { throw 'Module inspection failed' }
$exports | Set-Content "$out/libc-exports.txt" -Encoding utf8NoBOM
$listing = $exports -join "`n"
# NID algorithm from ps5link-sdk/linker/nid.c. The malloc entry is a control.
$salt = [byte[]](0x51,0x8d,0x64,0xa6,0x35,0xde,0xd8,0xc1,0xe6,0xb0,0x39,0xb1,0xc3,0xe5,0x52,0x30)
$names = @('malloc','__cxa_throw','__cxa_allocate_exception','__gxx_personality_v0',
    '_Unwind_Resume','_Znwm','_ZdlPv','_ZSt9terminatev','_ZNSt3__15mutex4lockEv',
    '_ZNSt3__15mutex6unlockEv','_ZNSt3__14cerrE','_ZTVSt9exception','_ZNSt12length_errorD1Ev')
$report = foreach ($name in $names) {
    $hash = [Security.Cryptography.SHA1]::HashData([byte[]]([Text.Encoding]::UTF8.GetBytes($name) + $salt))
    $shortHash = [byte[]]$hash[0..7]
    [Array]::Reverse($shortHash)
    $nid = [Convert]::ToBase64String($shortHash).TrimEnd('=').Replace('/','-')
    [pscustomobject]@{symbol=$name; nid=$nid; exported=$listing.Contains("  $nid  ")}
}
if (!$report[0].exported -or $report[0].nid -ne 'gQX+4GDQjpM') { throw 'NID control failed' }
$report | ConvertTo-Json | Set-Content "$out/libc-cpp-export-audit.json" -Encoding utf8NoBOM
$report | Format-Table -AutoSize
Write-Host 'Export presence does not validate execution or C++ library ABI compatibility.'
