[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/video-presenter"
New-Item -ItemType Directory -Force $out | Out-Null
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$sources=@('blit_vertices','agc_blit','gpu_fence','gpu_wait_service','color_target','gpu_color_target','texture_layout','depth_layout','depth_target','gpu_depth_target','video_presenter','present_rect')|ForEach-Object{"$PSScriptRoot/gpu/$_.cpp"}
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror -DMKW_VIDEO_PRESENTER_TEST=1 "-I$PSScriptRoot/gpu" @sources "$PSScriptRoot/tests/agc_blit.cpp" "$PSScriptRoot/tests/video_presenter.cpp" -o "$out/test.exe" *> "$out/build.log"
if($LASTEXITCODE -ne 0){throw 'Video presenter host build failed'}
$saved=$env:PATH
try{$env:PATH="$llvm;$saved";& "$out/test.exe" 2>&1|Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'Video presenter host test failed'}}finally{$env:PATH=$saved}
