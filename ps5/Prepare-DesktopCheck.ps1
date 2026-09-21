[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/.tools/desktop-check"
New-Item -ItemType Directory -Force $out | Out-Null
# Verification-only dependencies, pinned by AuroraDawnProvider.cmake and
# Aurora's extern/CMakeLists.txt. They are absent from every native PS5 target.
$packages = @(
    @{file='dawn-windows-amd64.tar.gz'; url='https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-windows-amd64.tar.gz'; hash='13BE9CFF8B9B179C42DCD16AEABB6EFFCC8F0DFDCC14463EDA2A5CAEDA225142'},
    @{file='abseil-20240722.0.tar.gz'; url='https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz'; hash='F50E5AC311A81382DA7FA75B97310E4B9006474F9560AC46F54A9967F07D4AE3'}
)
foreach ($package in $packages) {
    $archive = "$out/$($package.file)"
    if (!(Test-Path -LiteralPath $archive)) {
        & curl.exe --connect-timeout 5 --max-time 180 --location --fail --silent --show-error $package.url -o $archive
        if ($LASTEXITCODE -ne 0) { throw "Cannot download $($package.file)" }
    }
    if ((Get-FileHash -LiteralPath $archive).Hash -ne $package.hash) { throw "Checksum mismatch: $archive" }
}
New-Item -ItemType Directory -Force "$out/dawn" | Out-Null
& tar.exe -xf "$out/dawn-windows-amd64.tar.gz" -C "$out/dawn" ./include
if ($LASTEXITCODE -ne 0) { throw 'Dawn header extraction failed' }
& tar.exe -xf "$out/abseil-20240722.0.tar.gz" -C $out
if ($LASTEXITCODE -ne 0) { throw 'Abseil extraction failed' }
Write-Host 'Desktop regression headers ready; no GPU library installed or linked into PS5.'
