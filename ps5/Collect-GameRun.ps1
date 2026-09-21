[CmdletBinding()]
param(
    # Local folder name under artifacts/game-native for this run.
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9._-]+$')][string]$Name,
    [string]$ConsoleIp = '127.0.0.1'
)
# Close PPSA99611, make its latest run log readable over FTP (the FTP payload can
# run with other credentials than the game after a console restart), download
# the logs and print the frame-statistics summary.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dotnet = "$root/.tools/dotnet/dotnet.exe"
$bindgen = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
$close = "$root/artifacts/probe-launcher/20260915-032436-096/launcher.elf"
& $dotnet $bindgen payload --send --host $ConsoleIp --port 9021 --file $close | Out-Host
if ($LASTEXITCODE -ne 0) { throw 'Close payload send failed' }
Start-Sleep -Seconds 8

$run = python -c "import ftplib;f=ftplib.FTP();f.connect('$ConsoleIp',2120,timeout=60);f.login();f.encoding='utf-8';import re;n=[x.rsplit('/',1)[-1] for x in f.nlst('/data/PPSA99611/UserData/Logs')];print(max((x for x in n if re.fullmatch(r'[a-z_]+_[0-9]+_pid[0-9]+',x)),key=lambda x:int(x.rsplit('_',2)[1])));f.quit()"
if ($LASTEXITCODE -ne 0 -or !$run) { throw 'Cannot list run logs' }
Write-Host "Latest run: $run"
& "$PSScriptRoot/Build-LogAccessDiagnostic.ps1" -Run $run | Out-Host
$diag = Get-ChildItem "$root/artifacts/log-access" -Directory | Sort-Object LastWriteTime | Select-Object -Last 1
& $dotnet $bindgen payload --send --host $ConsoleIp --port 9021 --file "$($diag.FullName)/diagnostic.elf" | Out-Host
Start-Sleep -Seconds 3

$out = "$root/artifacts/game-native/$Name"
New-Item -ItemType Directory -Force $out | Out-Null
$script = @"
import ftplib, io, sys
run = '/data/PPSA99611/UserData/Logs/$run'
f = ftplib.FTP(); f.connect('$ConsoleIp', 2120, timeout=60); f.login(); f.encoding = 'utf-8'
for name in ('stderr.log', 'console.log', 'crash_exception.txt'):
    data = io.BytesIO()
    try:
        f.retrbinary('RETR ' + run + '/' + name, data.write)
    except Exception as error:
        print(name, 'absent:', error); continue
    open(r'$out/' + name, 'wb').write(data.getvalue()); print(name, len(data.getvalue()), 'bytes')
f.quit()
"@
python -c $script
if ($LASTEXITCODE -ne 0) { throw 'Log download failed' }
python "$PSScriptRoot/tools/summarize_frame_stats.py" $out

