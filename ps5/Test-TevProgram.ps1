[CmdletBinding()]
param([Parameter(Mandatory)][string]$Python)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$out="$root/artifacts/tev-program"
New-Item -ItemType Directory -Force $out | Out-Null
$includes=@("-I$root/Wiicompiled/aurora-main/include","-I$root/Wiicompiled/aurora-main/lib","-I$root/.tools/game-deps/fmt-11.1.4/include")
$flags=@('-std=c++20','-O2','-march=x86-64-v3','-fno-fast-math','-ffp-contract=off','-DNDEBUG','-DMKW_PLATFORM_PS5=1','-DTARGET_PC=1','-include','cstdlib')
$sources=@("$PSScriptRoot/gpu/gx_tev_program.cpp","$PSScriptRoot/tests/tev_program.cpp")
& "$llvm/clang++.exe" @flags @includes @sources -o "$out/fixture.exe"
if($LASTEXITCODE -ne 0){throw 'TEV fixture compilation failed'}
& "$llvm/clang++.exe" @flags @includes -DMKW_TEV_DLL -shared @sources -o "$out/stage.dll"
if($LASTEXITCODE -ne 0){throw 'TEV stage DLL compilation failed'}
$oldPath=$env:PATH
try {
    $env:PATH="$llvm;$oldPath"
    & "$out/fixture.exe" "$out/programs.bin" | Tee-Object "$out/encoder.log"
    if($LASTEXITCODE -ne 0){throw 'TEV snapshot checks failed'}
    & $Python -X utf8 "$PSScriptRoot/tests/test_tev_program.py"
    if($LASTEXITCODE -ne 0){throw 'TEV differential checks failed'}
} finally {$env:PATH=$oldPath}
