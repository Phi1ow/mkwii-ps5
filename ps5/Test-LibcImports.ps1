[CmdletBinding()]
param([Parameter(Mandatory)][string]$Module, [Parameter(Mandatory)][string]$LinkLog, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$generator = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
$exports = (& "$root/.tools/dotnet/dotnet.exe" $generator elf --file $Module --exports) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read libc exports' }
$salt = [byte[]](0x51,0x8d,0x64,0xa6,0x35,0xde,0xd8,0xc1,0xe6,0xb0,0x39,0xb1,0xc3,0xe5,0x52,0x30)
$report = @(foreach ($line in (Get-Content -LiteralPath $LinkLog)) {
    if ($line -match '^  -> (\S+)  \(libc\)$') {
        $symbol = $Matches[1]
        # SharpProspero's explicit alias reaches the original device export.
        $published = $symbol -replace '^__sp_device_', ''
        $hash = [Security.Cryptography.SHA1]::HashData([byte[]]([Text.Encoding]::UTF8.GetBytes($published) + $salt))
        $short = [byte[]]$hash[0..7]; [Array]::Reverse($short)
        $nid = [Convert]::ToBase64String($short).TrimEnd('=').Replace('/','-')
        [pscustomobject]@{symbol=$symbol;published=$published;nid=$nid;exported=$exports.Contains("  $nid  ")}
    }
})
if (!$report.Count) { throw 'No libc imports found; link output format may have changed' }
$report | ConvertTo-Json | Set-Content -LiteralPath $Output -Encoding utf8NoBOM
$missing = @($report | Where-Object { !$_.exported })
if ($missing.Count) { throw "Catalog imports absent from actual libc: $($missing.symbol -join ', ')" }
Write-Host "Verified all $($report.Count) linked libc imports against the supplied console module."
