[CmdletBinding()]
param(
    # Saved copy of the runtime memory layer to compare against (reference build).
    [string]$Reference = 'artifacts/equivalence/baseline-src',
    [string]$Seeds = "1,2,3,4,5,6,7,8",
    [uint64]$Steps = 2000000
)
# Differential test of the translated-code memory access layer: the same seeded
# program of guest accesses runs against the reference sources and the working
# tree (real PS5 GuestFlat backend, simulated kernel) and must print identical
# digests of every value, exception, deferred-read materialization, gather-pipe
# and executable write, and of the final guest RAM.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (![IO.Path]::IsPathRooted($Reference)) { $Reference = "$root/$Reference" }
$clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang++.exe" | Select-Object -First 1).FullName
$out = "$root/artifacts/memory-access-equivalence"
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($variant in @(@{name='reference'; base=$Reference}, @{name='candidate'; base=$root})) {
    $b = $variant.base
    & $clang -std=c++17 -O2 -march=x86-64-v3 -ffp-contract=off -fno-fast-math -Wall -Wno-unused-parameter -Wno-unused-function -DMKW_PLATFORM_PS5=1 `
        "-I$b/Wiicompiled/runtime/include" "-I$b/ps5/runtime" "-I$root/ps5/runtime" `
        "$PSScriptRoot/tests/memory_access_equivalence.cpp" "$b/ps5/runtime/guest_flat_memory_ps5.cpp" "$b/ps5/runtime/guest_memory_ps5.cpp" `
        "$b/Wiicompiled/runtime/src/memory.cpp" "$b/Wiicompiled/runtime/src/ppc_memory_helpers.cpp" -ldbghelp -o "$out/$($variant.name).exe"
    if ($LASTEXITCODE -ne 0) { throw "Compile failed for $($variant.name)" }
}
$previousPath = $env:PATH
$failures = 0
try {
    $env:PATH = "$(Split-Path $clang);$previousPath"
    foreach ($seed in ($Seeds -split ",")) {
        $a = & "$out/reference.exe" $seed $Steps
        if ($LASTEXITCODE -ne 0) { throw "reference run failed for seed $seed" }
        $c = & "$out/candidate.exe" $seed $Steps
        if ($LASTEXITCODE -ne 0) { throw "candidate run failed for seed $seed" }
        $same = ($a -join "`n") -eq ($c -join "`n")
        Write-Host ("seed {0}: {1} | {2}" -f $seed, ($c | Select-Object -Last 1), $(if ($same) { 'IDENTICAL' } else { 'DIFFERENT' }))
        if (!$same) { $failures++; $a | Set-Content "$out/reference-$seed.txt"; $c | Set-Content "$out/candidate-$seed.txt" }
    }
} finally { $env:PATH = $previousPath }
if ($failures) { throw "$failures seed(s) differ; see $out" }
Write-Host "PASS memory access equivalence: $(($Seeds -split ",").Count) seeds x $Steps steps identical"
