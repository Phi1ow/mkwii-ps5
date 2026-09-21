[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/texture-layout"
New-Item -ItemType Directory -Force $out | Out-Null
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
$env:NUGET_PACKAGES="$root/.tools/nuget"
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/agc_layout_reference" -c Release -- "$out/reference.bin"
if($LASTEXITCODE -ne 0){throw 'SharpProspero reference generation failed'}
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/texture_layout.cpp" "$PSScriptRoot/tests/texture_layout.cpp" -o "$out/test.exe"
if($LASTEXITCODE -ne 0){throw 'Texture layout test compilation failed'}
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/texture_layout.cpp" "$PSScriptRoot/gpu/gpu_texture.cpp" "$PSScriptRoot/tests/gpu_texture_memory.cpp" -o "$out/memory-test.exe"
if($LASTEXITCODE -ne 0){throw 'GPU texture ownership test compilation failed'}
$savedPath=$env:PATH
try {
    $env:PATH="$llvm;$savedPath"
    & "$out/test.exe" "$out/reference.bin" | Tee-Object "$out/host.log"
    if($LASTEXITCODE -ne 0){throw 'Texture layout differential test failed'}
    & "$out/memory-test.exe" | Tee-Object "$out/ownership.log"
    if($LASTEXITCODE -ne 0){throw 'GPU texture ownership test failed'}
} finally {$env:PATH=$savedPath}
