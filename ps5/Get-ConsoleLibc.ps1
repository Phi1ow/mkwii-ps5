[CmdletBinding()]
param(
    [string]$ConsoleIp = '127.0.0.1',
    [ValidateRange(1,65535)][int]$FtpPort = 1337,
    [string]$RemotePath = '/data/PPSA17221-app/sce_module/LIBC.PRX'
)
# The user explicitly authorized this module's export for the local AGC test.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$parsedIp = $null
if (![Net.IPAddress]::TryParse($ConsoleIp, [ref]$parsedIp) -or
    $parsedIp.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'Expected an IPv4 address' }
if ($RemotePath -notmatch '^/[^\r\n?#]+$' -or $RemotePath.Split('/') -contains '..') { throw 'Invalid remote path' }
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/.tools/console"
New-Item -ItemType Directory -Force $out | Out-Null
$partial = "$out/libc-$([Guid]::NewGuid().ToString('N')).partial"
$urlPath = (($RemotePath.Split('/') | ForEach-Object { [Uri]::EscapeDataString($_) }) -join '/')
& curl.exe --connect-timeout 4 --max-time 30 --fail --silent --show-error "ftp://${ConsoleIp}:$FtpPort$urlPath" -o $partial
if ($LASTEXITCODE) { throw 'Module download failed; no validated module was replaced.' }
$bytes = [IO.File]::ReadAllBytes($partial)
if ($bytes.Length -lt 64) { throw 'Downloaded module is truncated' }
if ([BitConverter]::ToString($bytes,0,4) -notin @('7F-45-4C-46', '54-14-F5-EE', '4F-15-3D-1D')) {
    throw 'Downloaded file is neither ELF nor a known SELF container'
}
$tool = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
& "$root/.tools/dotnet/dotnet.exe" $tool self --inspect --file $partial
if ($LASTEXITCODE) { throw 'Module inspection failed' }
Copy-Item -LiteralPath $partial -Destination "$out/libc.prx"
@{
    console = $ConsoleIp; remotePath = $RemotePath; ftpPort = $FtpPort
    sha256 = (Get-FileHash "$out/libc.prx").Hash
    firmwareCompatibilityVerified = $false
    use = 'User-authorized local AGC test only'
} | ConvertTo-Json | Set-Content "$out/libc-source.json" -Encoding utf8NoBOM
Write-Host "Downloaded and inspected: $out/libc.prx (hardware compatibility still unverified)"

