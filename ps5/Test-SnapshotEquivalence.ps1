[CmdletBinding()]
param(
    # Git revision whose ps5/gpu/gx_geometry.cpp is the reference.
    [string]$Reference = 'HEAD',
    [string]$Seeds = '1,2,3,4',
    [int]$Groups = 200000
)
# Differential test of the merged-draw vertex snapshot: identical digests of
# every output for the same seeded random groups, reference versus working tree.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$llvm = "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin"
$aurora = "$root/Wiicompiled/aurora-main"
$out = "$root/artifacts/snapshot-equivalence"
New-Item -ItemType Directory -Force $out | Out-Null
$referenceSource = & git -C $root show "${Reference}:ps5/gpu/gx_geometry.cpp"
if ($LASTEXITCODE -ne 0) { throw "Cannot read ps5/gpu/gx_geometry.cpp at $Reference" }
[IO.File]::WriteAllText("$out/gx_geometry_reference.cpp", ($referenceSource -join "`n") + "`n")
$flags = @('-std=c++20', '-O2', '-march=x86-64-v3', '-fno-fast-math', '-ffp-contract=off', '-DNDEBUG', '-DMKW_PLATFORM_PS5=1',
    '-DTARGET_PC=1', '-include', 'cstdlib', "-I$PSScriptRoot/gpu", "-I$aurora/include", "-I$aurora/lib",
    "-I$root/.tools/game-deps/fmt-11.1.4/include", "-I$root/.tools/game-deps/tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169/public")
foreach ($variant in @(@{name = 'reference'; source = "$out/gx_geometry_reference.cpp"},
                       @{name = 'candidate'; source = "$PSScriptRoot/gpu/gx_geometry.cpp"})) {
    & "$llvm/clang++.exe" @flags $variant.source "$PSScriptRoot/tests/snapshot_segments_equivalence.cpp" -o "$out/$($variant.name).exe"
    if ($LASTEXITCODE -ne 0) { throw "Compile failed for $($variant.name)" }
}
$savedPath = $env:PATH
$failures = 0
try {
    $env:PATH = "$llvm;$savedPath"
    foreach ($seed in ($Seeds -split ',')) {
        $a = & "$out/reference.exe" $seed $Groups
        $c = & "$out/candidate.exe" $seed $Groups
        $same = "$a" -eq "$c"
        Write-Host ("seed {0}: {1} | {2}" -f $seed, $c, $(if ($same) { 'IDENTICAL' } else { "DIFFERENT (reference: $a)" }))
        if (!$same) { $failures++ }
    }
} finally { $env:PATH = $savedPath }
if ($failures) { throw "$failures seed(s) differ" }
Write-Host "PASS snapshot equivalence: $(($Seeds -split ',').Count) seeds x $Groups groups identical"
