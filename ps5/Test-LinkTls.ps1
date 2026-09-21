[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out="$root/artifacts/link-tls"
New-Item -ItemType Directory -Force $out | Out-Null
$clang="$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin/clang.exe"
$objects=@()
foreach($suffix in @('a','b','c')){
    $object="$out/$suffix.o"; $objects+=$object
    & $clang --no-default-config --target=x86_64-unknown-freebsd -ffreestanding -fPIC -ftls-model=global-dynamic -O2 -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -c "$PSScriptRoot/tests/tls_shared_$suffix.cpp" -o $object
    if($LASTEXITCODE -ne 0){throw 'TLS fixture compilation failed'}
}
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/link_tls_check" -c Release -- @objects
if($LASTEXITCODE -ne 0){throw 'TLS linker regression failed'}
