[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$name = 'clang+llvm-18.1.8-x86_64-pc-windows-msvc'
$archive = "$root/.tools/$name.tar.xz"
$checksum = '22C5907DB053026CC2A8FF96D21C0F642A90D24D66C23C6D28EE7B1D572B82E8'
if (!(Test-Path -LiteralPath $archive)) {
    & curl.exe --connect-timeout 5 --max-time 300 --location --fail --silent --show-error "https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/$name.tar.xz" -o $archive
    if ($LASTEXITCODE -ne 0) { throw 'LLVM shader assembler download failed' }
}
if ((Get-FileHash -LiteralPath $archive).Hash -ne $checksum) { throw 'LLVM shader archive checksum mismatch' }
if (!(Test-Path "$root/.tools/$name/bin/llvm-mc.exe") -or !(Test-Path "$root/.tools/$name/bin/llvm-objcopy.exe") -or !(Test-Path "$root/.tools/$name/bin/clang.exe") -or !(Test-Path "$root/.tools/$name/bin/llc.exe")) {
    & tar.exe -xf $archive -C "$root/.tools" "$name/bin/llvm-mc.exe" "$name/bin/llvm-objcopy.exe" "$name/bin/clang.exe" "$name/bin/llc.exe" "$name/bin/*.dll"
    if ($LASTEXITCODE -ne 0) { throw 'LLVM shader tools extraction failed' }
}
$version = (& "$root/.tools/$name/bin/llvm-mc.exe" --version) -join "`n"
if ($LASTEXITCODE -ne 0 -or $version -notmatch 'amdgcn') { throw 'LLVM assembler lacks AMDGPU' }
Write-Host 'LLVM 18.1.8 AMDGPU assembler ready; no system installation changed.'
