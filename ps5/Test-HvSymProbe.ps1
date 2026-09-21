$ErrorActionPreference="Stop"
$root = Split-Path $PSScriptRoot -Parent
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/hv-symprobe"
New-Item -ItemType Directory -Force $out | Out-Null
$flags = @("--no-default-config","--target=x86_64-unknown-freebsd","-ffreestanding","-fPIC","-fno-stack-protector","-O2","-std=c++17","-fno-exceptions","-fno-rtti","-Wno-unused")
& $clang @flags -c "$PSScriptRoot/diagnostics/hv_symprobe.cpp" -o "$out/probe.o"
& "$root/.tools/dotnet/dotnet.exe" "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll" link --obj "$out/probe.o" --self-contained --kind payload --return-on-exit --out "$out/probe.elf"
Write-Host "=== LINK DONE ==="