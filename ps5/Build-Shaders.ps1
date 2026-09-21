[CmdletBinding()]
param([Parameter(Mandatory)][string]$Python)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bin = "$root/.tools/clang+llvm-18.1.8-x86_64-pc-windows-msvc/bin"
$out = "$root/artifacts/agc-shaders"
$sdk = "$root/ps5link-sdk/shaders"
New-Item -ItemType Directory -Force $out | Out-Null
function Run([string]$Tool, [string[]]$Arguments) {
    & $Tool @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Shader tool failed: $Tool" }
}
Run $Python @("$sdk/tools/agcpack.py", 'texture-container', "$sdk/third_party/sharpprospero/mesh_ps.sb", "$out/texture_container.sb")
Run "$bin/llvm-mc.exe" @('-triple=amdgcn-amd-amdhsa', '-mcpu=gfx1030', '-filetype=obj', '-o', "$out/textured_p.o", "$sdk/src/textured_p.s")
Run "$bin/llvm-objcopy.exe" @('--dump-section', ".text=$out/textured_p.bin", "$out/textured_p.o", "$out/textured_p.stripped.o")
Run $Python @("$sdk/tools/agcpack.py", 'pack', "$out/texture_container.sb", "$out/textured_p.bin", "$out/textured_p.sb", '--vgprs', '4')
Run $Python @("$sdk/tools/agcpack.py", 'array', "$out/textured_p_sb.h", 'textured_p_sb', "$out/textured_p.sb")
@{source='ps5link-sdk/shaders/src/textured_p.s';sourceSha256=(Get-FileHash "$sdk/src/textured_p.s").Hash;containerSha256=(Get-FileHash "$out/textured_p.sb").Hash;assembler='LLVM 18.1.8 gfx1030';packed=$true;nativeTested=$false;gxTevImplemented=$false} | ConvertTo-Json | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host 'Native AGC textured shader assembled and packed; console validation remains.'
