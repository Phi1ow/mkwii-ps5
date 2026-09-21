[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$llvm = "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out = "$root/artifacts/gx-frontend"
New-Item -ItemType Directory -Force $out | Out-Null
$aurora = "$root/Wiicompiled/aurora-main"
$fmt = "$root/.tools/game-deps/fmt-11.1.4"
$tracy = "$root/.tools/game-deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public"
$sources = @('GXTev','GXPixel','GXCull','GXBump','GXLighting','frontend') | ForEach-Object { "$aurora/lib/dolphin/gx/$_.cpp" }
& "$llvm/clang++.exe" -std=c++20 -O2 -DMKW_PLATFORM_PS5=1 -DTARGET_PC=1 -DFMT_USE_FALLBACK_FILE=1 -DFMT_USE_FCNTL=0 -include cstdlib "-I$aurora/include" "-I$aurora/lib" "-I$fmt/include" "-I$tracy" @sources "$aurora/lib/gx/fifo.cpp" "$aurora/lib/logging.cpp" "$fmt/src/format.cc" "$PSScriptRoot/tests/gx_frontend.cpp" -luser32 -o "$out/test.exe" *> "$out/build.log"
if ($LASTEXITCODE -ne 0) { throw 'GX producer compile failed; see artifacts/gx-frontend/build.log' }
$savedPath = $env:PATH
try {
    $env:PATH = "$llvm;$savedPath"
    & "$out/test.exe" | Tee-Object "$out/host.log"
    if ($LASTEXITCODE -ne 0) { throw 'GX producer stream checks failed' }
} finally { $env:PATH = $savedPath }

