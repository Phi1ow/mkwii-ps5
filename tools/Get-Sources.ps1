[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content "$root/sources.lock.json" -Raw | ConvertFrom-Json
foreach ($dependency in $lock.repositories) {
    $name = $dependency.directory
    $destination = Join-Path $root $name
    if (Test-Path -LiteralPath $destination) {
        throw "$destination already exists. Use a fresh checkout; existing work is never overwritten."
    }
}
foreach ($dependency in $lock.repositories) {
    $name = $dependency.directory
    $destination = Join-Path $root $name
    & git clone --no-checkout $dependency.url $destination
    if ($LASTEXITCODE) { throw "Clone failed: $name" }
    & git -C $destination checkout --detach $dependency.commit
    if ($LASTEXITCODE) { throw "Checkout failed: $name" }
    $patch = "$root/patches/$name.patch"
    if ((Get-Item -LiteralPath $patch).Length -gt 0) {
        & git -C $destination apply --check $patch
        if ($LASTEXITCODE) { throw "Patch check failed: $name" }
        & git -C $destination apply $patch
        if ($LASTEXITCODE) { throw "Patch failed: $name" }
    }
    $additions = "$root/dependency-additions/$name"
    if (Test-Path -LiteralPath $additions) {
        foreach ($file in Get-ChildItem -LiteralPath $additions -File -Recurse) {
            $relative = [IO.Path]::GetRelativePath($additions, $file.FullName)
            $target = Join-Path $destination $relative
            if (Test-Path -LiteralPath $target) { throw "Addition would overwrite: $target" }
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target
        }
    }
}
Write-Host 'Pinned dependencies and PS5 modifications restored. Do not also apply the historical ps5/patches series.'
