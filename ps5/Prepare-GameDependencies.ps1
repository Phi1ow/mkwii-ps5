[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/.tools/game-deps"
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($dependency in (Get-Content "$PSScriptRoot/toolchain/game-dependencies.json" -Raw | ConvertFrom-Json)) {
    $archive = "$out/$($dependency.archive)"
    if (!(Test-Path -LiteralPath $archive)) {
        & curl.exe --connect-timeout 5 --max-time 180 --location --fail --silent --show-error $dependency.url -o $archive
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $($dependency.archive)" }
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $dependency.sha256) { throw "Checksum mismatch: $archive" }
    if (!(Test-Path -LiteralPath "$out/$($dependency.directory)")) {
        & tar.exe -xf $archive -C $out
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $archive" }
    }
}
Write-Host 'Prepared the SDL3, ImGui, xxHash, fmt and Tracy versions declared by WiiCompiled.'
