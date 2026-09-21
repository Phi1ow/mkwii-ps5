[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$llvm = "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out = "$root/artifacts/texture-decode"
New-Item -ItemType Directory -Force $out | Out-Null
$aurora = "$root/Wiicompiled/aurora-main"
$fmt = "$root/.tools/game-deps/fmt-11.1.4"
& "$llvm/clang++.exe" -std=c++20 -O2 -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -DMKW_HOST_TEXTURE_TEST -DFMT_USE_FALLBACK_FILE=1 -DFMT_USE_FCNTL=0 -include cstdlib "-I$aurora/include" "-I$aurora/lib" "-I$fmt/include" "$aurora/lib/gfx/texture_convert.cpp" "$aurora/lib/logging.cpp" "$fmt/src/format.cc" "$PSScriptRoot/tests/texture_decode.cpp" -luser32 -o "$out/test.exe" *> "$out/build.log"
if ($LASTEXITCODE -ne 0) { throw 'Texture test compile failed; see artifacts/texture-decode/build.log' }
$savedPath = $env:PATH
try {
    $env:PATH = "$llvm;$savedPath"
    & "$out/test.exe" | Tee-Object "$out/host.log"
    if ($LASTEXITCODE -ne 0) { throw 'Texture golden vectors failed' }
} finally { $env:PATH = $savedPath }
