[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [string]$ConsoleIp = '127.0.0.1',
    [ValidateRange(1,65535)][int]$FtpPort = 1337,
    [ValidateRange(60,1800)][int]$TransferTimeoutSeconds = 60
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$parsedIp = $null
if (![Net.IPAddress]::TryParse($ConsoleIp, [ref]$parsedIp) -or
    $parsedIp.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) { throw 'Expected an IPv4 address' }
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$manifest = Get-Content "$build/build.json" -Raw | ConvertFrom-Json
if($manifest.PSObject.Properties['externalGameData']){
    $data=Get-Content -LiteralPath $manifest.externalGameDataTransfer -Raw | ConvertFrom-Json
    if(!$data.complete -or !$data.roundTripSha256Verified -or ($data.destination+'/DATA') -ne $manifest.externalGameData){
        throw 'External game DATA transfer is not complete and verified; no package uploaded'
    }
}
if (!$manifest.completePackage) { throw 'Incomplete package; rebuild with a compatible libc.prx first.' }
$title = $manifest.titleId
if ($title -notmatch '^[A-Z]{4}[0-9]{5}$') { throw 'Invalid title ID' }
$package = "$build/$title"
$required = @('eboot.bin', 'sce_sys/param.json', 'sce_sys/icon0.png', 'sce_module/libc.prx')
if($manifest.PSObject.Properties['auroraBootstrap'] -and $manifest.auroraBootstrap){
    $required+=@('shaders/gx_vertex.sb','shaders/gx_pixel.sb','shaders/copy_vertex.sb','shaders/copy_pixel.sb')
}
$required=@($required + @($manifest.files.PSObject.Properties.Name) | Select-Object -Unique)
foreach($file in $required){
    if($file -notmatch '^[A-Za-z0-9_./-]+$' -or $file.StartsWith('/') -or ($file.Split('/') | Where-Object {$_ -in @('','.', '..')})){
        throw "Unsafe package relative path: $file"
    }
}
foreach ($file in $required) {
    $expected = $manifest.files.PSObject.Properties[$file]
    if (!$expected -or !(Test-Path "$package/$file") -or
        (Get-FileHash "$package/$file").Hash -ne $expected.Value) { throw "Package validation failed: $file" }
}
$param = Get-Content "$package/sce_sys/param.json" -Raw | ConvertFrom-Json
if ($param.titleId -ne $title) { throw 'Title does not match the build manifest' }
$base = "ftp://${ConsoleIp}:$FtpPort"
# This console's observed ShadowMount configuration scans /data. Do not edit it.
$listing = & curl.exe --connect-timeout 4 --max-time 15 --fail --silent --show-error --list-only "$base/data/"
if ($LASTEXITCODE -ne 0) { throw 'FTP unavailable; nothing uploaded' }
if ($listing -contains $title) { throw "/data/$title already exists; choose a new TitleId and rebuild." }
$stage = "/data/tmp/wiicompiled-$title-$([Guid]::NewGuid().ToString('N'))"
foreach ($file in $required) {
    & curl.exe --connect-timeout 4 --max-time $TransferTimeoutSeconds --fail --silent --show-error --ftp-create-dirs --upload-file "$package/$file" "$base$stage/$file"
    if ($LASTEXITCODE -ne 0) { throw "Upload failed: $file. Partial files remain only at $stage." }
}
# FTP does not preserve host executable permissions. Request SITE CHMOD, but
# zftpd on this console has returned success while stat still showed 0666.
# Treat this as a request only; verify with the native file-access diagnostic.
foreach ($file in @('eboot.bin', 'sce_module/libc.prx')) {
    & curl.exe --connect-timeout 4 --max-time 15 --fail --silent --show-error --list-only --quote "SITE CHMOD 755 $stage/$file" "$base$stage/" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Cannot set executable permissions on $file; staging retained at $stage" }
}
# Recheck the destination immediately before the directory rename.
$listing = & curl.exe --connect-timeout 4 --max-time 15 --fail --silent --show-error --list-only "$base/data/"
if ($LASTEXITCODE -ne 0 -or $listing -contains $title) { throw "Cannot publish safely; staging retained at $stage" }
& curl.exe --connect-timeout 4 --max-time 15 --fail --silent --show-error --list-only --quote "RNFR $stage" --quote "RNTO /data/$title" "$base/data/"
if ($LASTEXITCODE -ne 0) { throw "FTP rename failed; inspect staging at $stage" }
# Verify transferred bytes before reporting a successful installation.
foreach ($file in $required) {
    $check = "$build/verify-$($file.Replace('/', '-'))"
    & curl.exe --connect-timeout 4 --max-time $TransferTimeoutSeconds --fail --silent --show-error "$base/data/$title/$file" -o $check
    if ($LASTEXITCODE -ne 0 -or (Get-FileHash $check).Hash -ne $manifest.files.PSObject.Properties[$file].Value) {
        throw "Transferred file could not be verified: $file"
    }
}
@{ console = $ConsoleIp; path = "/data/$title"; transferredAndVerified = $true; launched = $false; executablePermissionsVerified = $false } |
    ConvertTo-Json | Set-Content "$build/deployment.json" -Encoding utf8NoBOM
$displayName = $param.localizedParameters.'en-US'.titleName
Write-Host "Transferred and verified: /data/$title. Launch '$displayName' from the console after ShadowMount scans it."
Write-Warning 'FTP byte verification does not verify executable permissions. Run the native file-access diagnostic before launching.'

