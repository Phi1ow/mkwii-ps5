[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$lock = Get-Content "$PSScriptRoot/toolchain/sources.json" -Raw | ConvertFrom-Json
$sources = "$root/.tools/cxx-src"
$sysroot = [IO.Path]::GetFullPath("$root/.tools/ps5-sysroot")
New-Item -ItemType Directory -Force $sources, $sysroot | Out-Null
function Fetch([string]$Url, [string]$Path, [string]$Sha256) {
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
        & curl.exe --connect-timeout 5 --max-time 180 --location --fail --silent --show-error $Url -o $Path
        if ($LASTEXITCODE -ne 0) { throw "Download failed: $Url" }
    }
    if ((Get-FileHash -LiteralPath $Path).Hash -ne $Sha256) { throw "Checksum mismatch: $Path" }
}
foreach ($archive in $lock.archives) {
    $path = "$sources/$($archive.name)"
    Fetch "https://github.com/llvm/llvm-project/releases/download/llvmorg-$($lock.llvmRelease)/$($archive.name)" $path $archive.sha256
    $directory = $archive.name.Replace('.tar.xz', '')
    if (!(Test-Path "$sources/$directory")) {
        & tar.exe -xf $path -C $sources
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed: $path" }
    }
}
if (!(Test-Path "$sources/cmake")) { Copy-Item -LiteralPath "$sources/cmake-18.1.8.src" -Destination "$sources/cmake" -Recurse }
foreach ($helper in $lock.cmakeHelpers) {
    Fetch "https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-$($lock.llvmRelease)/runtimes/cmake/Modules/$($helper.name)" "$sources/runtimes/cmake/Modules/$($helper.name)" $helper.sha256
}
$base = "$sources/freebsd-9.3-base.txz"
Fetch $lock.cHeaders.url $base $lock.cHeaders.sha256
if (!(Test-Path "$sysroot/headers-ready.json")) {
    $entries = & tar.exe -tvf $base ./usr/include
    if ($LASTEXITCODE -ne 0) { throw 'Cannot list header archive' }
    $links = @()
    $exclude = @()
    foreach ($line in $entries) {
        if ($line -match '(\./usr/include/\S+) -> (.+)$') {
            $links += @{path=$Matches[1];target=$Matches[2]}
            $exclude += @('--exclude', $Matches[1])
        }
    }
    # Windows need not create Unix symlinks. Materialize their in-tree targets
    # after extracting regular files; validate both resolved paths first.
    & tar.exe -xf $base -C $sysroot @exclude ./usr/include
    if ($LASTEXITCODE -ne 0) { throw 'Header extraction failed' }
    foreach ($link in $links) {
        $destination = [IO.Path]::GetFullPath((Join-Path $sysroot $link.path))
        $source = [IO.Path]::GetFullPath((Join-Path (Split-Path $destination) $link.target))
        if (!$destination.StartsWith($sysroot + '\') -or !$source.StartsWith($sysroot + '\')) { throw 'Header link escapes sysroot' }
        Copy-Item -LiteralPath $source -Destination $destination
    }
    @{sourceHash=$lock.cHeaders.sha256;nativeAbiVerified=$false} | ConvertTo-Json | Set-Content "$sysroot/headers-ready.json" -Encoding utf8NoBOM
}
Write-Host 'C++ sources and C headers prepared. This is not yet a validated PS5 standard library.'
