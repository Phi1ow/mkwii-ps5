[CmdletBinding()]
param(
    # Folder produced by Build-GameExecutable.ps1.
    [Parameter(Mandatory)][string]$ExecutableDirectory,
    # SHA-256 of the eboot currently installed in /data/PPSA99611 (update tool refuses a mismatch).
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$PreviousSha256,
    [string]$ConsoleIp = '127.0.0.1'
)
# Publish a game executable to PPSA99611 and launch it, using the verified steps
# of PORTAGE.md: close/absence evidence, update with backup, execute permission
# repair, launch. Does not touch DATA, NAND, Config.toml or the shaders.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dotnet = "$root/.tools/dotnet/dotnet.exe"
$bindgen = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
$close = "$root/artifacts/probe-launcher/20260915-032436-096/launcher.elf"
$launch = "$root/artifacts/probe-launcher/20260915-032436-674/launcher.elf"
$permissions = "$root/artifacts/file-access/20260915-000437-manual/diagnostic.elf"
function Send-Payload([string]$file) {
    & $dotnet $bindgen payload --send --host $ConsoleIp --port 9021 --file $file | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Payload send failed: $file" }
}
function Capture([int]$seconds) {
    # Use the file this capture reports, never "the newest klog": klogsrv serves
    # one client, and another capture running in parallel would be picked instead.
    # Capture-Klog reports through Write-Host (information stream 6).
    $output = & "$PSScriptRoot/Capture-Klog.ps1" -ConsoleIp $ConsoleIp -Seconds $seconds 6>&1 | ForEach-Object { "$_" }
    $output | Out-Host
    $line = $output | Where-Object { $_ -match '^Captured \d+ bytes\. Log: (.+)$' } | Select-Object -Last 1
    if (!$line -or $line -notmatch '^Captured \d+ bytes\. Log: (.+)$') { throw 'Klog capture did not report its file' }
    return $Matches[1]
}

Send-Payload $close
Start-Sleep -Seconds 3
$evidence = Capture 8
python "$PSScriptRoot/tools/update_game_executable.py" $ExecutableDirectory --previous-sha256 $PreviousSha256.ToLower() --quiescence-log $evidence
if ($LASTEXITCODE -ne 0) { throw 'Executable update failed; inspect artifacts/game-updates' }
$update = Get-ChildItem "$root/artifacts/game-updates" -Directory | Sort-Object LastWriteTime | Select-Object -Last 1
$result = Get-Content "$($update.FullName)/result.json" -Raw | ConvertFrom-Json
if ($result.phase -ne 'published-verified') { throw "Update phase is $($result.phase)" }
Write-Host "Published $($result.sha256)"

Send-Payload $permissions
Start-Sleep -Seconds 2
$log = Capture 8
$mode = Select-String -LiteralPath $log -Pattern 'app/PPSA99611/eboot.bin .*mode=0x81ed' -Quiet
if (!$mode) { throw "Execute permission not confirmed in $log" }

Send-Payload $launch
$log = Capture 15
if (!(Select-String -LiteralPath $log -Pattern 'EXEC /app0/eboot.bin' -Quiet)) { throw "No EXEC seen in $log" }
Write-Host "Launched $($result.sha256) at $(Get-Date -Format HH:mm:ss)"

