[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/gpu-fence"
$rows=Get-Content "$out/native-99548.log" | Where-Object { $_ -match '^\[mkw-fence-packet\] 00 ' }
$words=@($rows | ForEach-Object { [Convert]::ToUInt32(($_ -split ' ')[3],16) })
if($words.Count -ne 64 -or $words[10] -ne [Convert]::ToUInt32('c0064900',16)){throw 'Unexpected captured AGC flip packet'}
$bytes=[Collections.Generic.List[byte]]::new()
foreach($word in $words[10..17]){$bytes.AddRange([BitConverter]::GetBytes([uint32]$word))}
[IO.File]::WriteAllBytes("$out/captured-release.bin",$bytes.ToArray())
$llvm="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
& "$llvm/clang++.exe" -std=c++20 -O2 -Wall -Wextra -Werror "-I$PSScriptRoot/gpu" "$PSScriptRoot/gpu/gpu_fence.cpp" "$PSScriptRoot/tests/gpu_fence.cpp" -o "$out/test.exe" *> "$out/host-build.log"
if($LASTEXITCODE -ne 0){throw 'GPU completion host compile failed'}
$savedPath=$env:PATH
try{$env:PATH="$llvm;$savedPath";& "$out/test.exe" "$out/captured-release.bin" 2>&1 | Tee-Object "$out/host.log";if($LASTEXITCODE -ne 0){throw 'GPU completion encoding failed'}}finally{$env:PATH=$savedPath}
