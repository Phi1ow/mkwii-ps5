[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/artifacts/linker-frame-tests"
New-Item -ItemType Directory -Force $out | Out-Null
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName
foreach ($part in @('first','second')) {
    $extra = @(); if ($part -eq 'second') { $extra = @('-DSECOND') }
    & $clang --no-default-config --target=x86_64-unknown-freebsd -fPIC @extra -c "$PSScriptRoot/tests/linker_frames/fixture.S" -o "$out/$part.o"
    if ($LASTEXITCODE -ne 0) { throw 'Fixture compilation failed' }
}
$env:DOTNET_CLI_HOME = "$root/.tools/dotnet-home"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tests/linker_frames" -c Release -- "$out/first.o" "$out/second.o"
if ($LASTEXITCODE -ne 0) { throw 'Frame regression failed' }
@{passed=$true;inputFrames=3;keptFrames=2;ignoredRelocations=2} | ConvertTo-Json | Set-Content "$out/result.json"
