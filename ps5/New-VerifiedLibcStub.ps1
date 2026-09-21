[CmdletBinding()]
param([Parameter(Mandatory)][string]$Module, [Parameter(Mandatory)][string[]]$Objects, [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dotnet = "$root/.tools/dotnet/dotnet.exe"
$generator = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
$nm = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/llvm-nm.exe" | Select-Object -First 1).FullName
$response = "$Output.objects.rsp"
$Objects | ForEach-Object { '"' + ([IO.Path]::GetFullPath($_).Replace('\','/')) + '"' } |
    Set-Content -LiteralPath $response -Encoding utf8NoBOM
$lines = & $nm -u --format=posix "@$response"
if ($LASTEXITCODE -ne 0) { throw 'Cannot read undefined ELF symbols' }
# ELF symbols are case-sensitive: _Exit and _exit have different NIDs. The
# default PowerShell comparison can silently discard one as input order changes.
$names = @($lines | ForEach-Object { if ($_ -match '^(\S+) U(?:\s|$)') { $Matches[1] } } | Sort-Object -Unique -CaseSensitive)
$exports = (& $dotnet $generator elf --file $Module --exports) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect module exports' }
$salt = [byte[]](0x51,0x8d,0x64,0xa6,0x35,0xde,0xd8,0xc1,0xe6,0xb0,0x39,0xb1,0xc3,0xe5,0x52,0x30)
$report = foreach ($name in $names) {
    $hash = [Security.Cryptography.SHA1]::HashData([byte[]]([Text.Encoding]::UTF8.GetBytes($name) + $salt))
    $shortHash = [byte[]]$hash[0..7]; [Array]::Reverse($shortHash)
    $nid = [Convert]::ToBase64String($shortHash).TrimEnd('=').Replace('/','-')
    [pscustomobject]@{symbol=$name;nid=$nid;exported=$exports.Contains("  $nid  ")}
}
$verified = @($report | Where-Object exported | Select-Object -ExpandProperty symbol)
if (!$verified.Count) { throw 'No verified exports for this stub' }
$verified | Set-Content "$Output.names.txt" -Encoding utf8NoBOM
$report | ConvertTo-Json | Set-Content "$Output.audit.json" -Encoding utf8NoBOM
& $dotnet $generator stub --module $Module --names "$Output.names.txt" --out $Output
if ($LASTEXITCODE -ne 0) { throw 'Verified libc stub generation failed' }
