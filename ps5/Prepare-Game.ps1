[CmdletBinding()]
param([Parameter(Mandatory)][string]$DiscImage)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
$nod = "$root/.tools/nodtool.exe"
$assets = "$root/Wiicompiled/Assets"
if (!(Test-Path -LiteralPath $DiscImage -PathType Leaf)) { throw "Image missing: $DiscImage" }
$info = & $nod info $DiscImage 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw $info }
if ($info -notmatch '(?m)^Game ID: RMCP01\s*$') { throw "Expected Mario Kart Wii PAL RMCP01.`n$info" }
if (Test-Path "$assets/DATA") { throw 'Assets/DATA already exists; refusing to overwrite an extraction.' }
New-Item -ItemType Directory -Force $assets | Out-Null
& $nod extract -q -p data $DiscImage "$assets/DATA"
if ($LASTEXITCODE -ne 0) { throw 'Disc extraction failed. Inspect Assets/DATA before retrying.' }
$inputs = @(
    @{ source = 'sys/main.dol'; name = 'main.dol'; hash = '80d18895b39c63bd80f457398bfcbb91b7d16ac116a41a88967e954080155b05' },
    @{ source = 'files/rel/StaticR.rel'; name = 'StaticR.rel'; hash = '16d9d146112541fefea701ecb5bc1a496f9d50e4a752fbb5b6778e7c6399f67d' }
)
# Validate both inputs before exposing either at the translator's expected paths.
foreach ($input in $inputs) {
    $actual = (Get-FileHash "$assets/DATA/$($input.source)" -Algorithm SHA256).Hash
    if ($actual -ne $input.hash) { throw "Wrong game revision: $($input.name), SHA256 $actual" }
    if (Test-Path "$assets/$($input.name)") { throw "Input already exists: $($input.name)" }
}
foreach ($input in $inputs) {
    Copy-Item -LiteralPath "$assets/DATA/$($input.source)" -Destination "$assets/$($input.name)"
}
New-Item -ItemType Directory -Force "$root/artifacts" | Out-Null
@{ gameId = 'RMCP01'; disc = $DiscImage; inputs = $inputs; verified = $true } |
    ConvertTo-Json -Depth 4 | Set-Content "$root/artifacts/game-inputs.json" -Encoding utf8NoBOM
Write-Host 'Mario Kart Wii PAL: main.dol and StaticR.rel match the upstream manifest.'
