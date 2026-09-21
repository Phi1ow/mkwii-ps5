[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/shader-owner"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/shader_binary.cpp" "$PSScriptRoot/tests/shader_binary.cpp" -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Shader parser host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/test.exe" "$root/artifacts/gx-varyings/vertex.sb" "$root/artifacts/tev-fragment/tev_probe.sb" "$root/artifacts/gx-copy-shader/copy.sb" "$root/ps5link-sdk/shaders/third_party/sharpprospero/mesh_vs.sb" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Shader parser test failed'}}finally{$env:PATH=$saved}
