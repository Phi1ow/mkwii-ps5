[CmdletBinding()]
param([Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/posix_stub" -c Release -- "$root/artifacts/kernel-exports/result.json" $Output
if($LASTEXITCODE -ne 0){throw 'POSIX import candidate generation failed'}
