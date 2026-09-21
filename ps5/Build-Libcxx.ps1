[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    [string]$Ninja = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = "$root/artifacts/libcxx-build"
& $CMake -S "$PSScriptRoot/toolchain/libcxx" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/ps5-cxx.cmake" '-DCMAKE_BUILD_TYPE=Release' *> "$root/artifacts/libcxx-configure.log"
if ($LASTEXITCODE -ne 0) { throw 'libc++ configure failed; see artifacts/libcxx-configure.log' }
& $CMake --build $build --target cxx_static -j 6 *> "$root/artifacts/libcxx-build.log"
if ($LASTEXITCODE -ne 0) { throw 'libc++ build failed; see artifacts/libcxx-build.log' }
@{archiveSha256=(Get-FileHash "$build/lib/libc++.a").Hash;target='x86_64-unknown-freebsd9.3';nativeTested=$false} | ConvertTo-Json | Set-Content "$build/build-result.json" -Encoding utf8NoBOM
Write-Host 'Built libc++ and matching libc++abi. PS5 imports and runtime behavior still require validation.'
