[CmdletBinding()]
param(
    # Translated game build tree (artifacts/game-build-fprof for the profiling variant).
    [string]$GameBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = "$root/artifacts/game-link"
if (!$GameBuild) { $GameBuild = "$root/artifacts/game-build" }
New-Item -ItemType Directory -Force $out | Out-Null
$objects = @((Get-ChildItem "$root/artifacts/runtime-build/CMakeFiles/mkw_ps5_runtime.dir" -Recurse -File -Filter '*.o').FullName)
$expected = (Get-Content "$root/artifacts/runtime-build/runtime-graph.json" -Raw | ConvertFrom-Json).translationUnits
if ($objects.Count -ne $expected) { throw "Expected $expected runtime objects, got $($objects.Count); compile the full runtime first" }
foreach ($target in @('host_context','guest_memory','guest_flat','engine_memory','cxx_platform','cpu_baseline','game_data')) {
    $files = @(Get-ChildItem "$GameBuild/CMakeFiles/mkw_ps5_$target.dir" -Recurse -File -Filter '*.o')
    if (!$files.Count) { throw "Missing platform target $target" }
    $objects += $files.FullName
}
# Registrars only contribute static constructors; archive demand loading drops
# them. Include the complete generated base graph as primary objects so every
# translated function and its registration survive the final link.
$translated = @((Get-ChildItem "$GameBuild/CMakeFiles/mkw_ps5_translated.dir" -Recurse -File -Filter '*.o').FullName)
$gameGraph = Get-Content "$GameBuild/game-graph.json" -Raw | ConvertFrom-Json
if ($translated.Count -ne $gameGraph.translationUnits) { throw "Expected $($gameGraph.translationUnits) translated objects, got $($translated.Count)" }
$objects += $translated
# DIAGNOSTIC function profiler, present only in the profiling variant's tree.
if (Test-Path "$GameBuild/CMakeFiles/mkw_ps5_fprof.dir") {
    $objects += @((Get-ChildItem "$GameBuild/CMakeFiles/mkw_ps5_fprof.dir" -Recurse -File -Filter '*.o').FullName)
}
$archives = @("$root/artifacts/libcxx-build/lib/libc++.a")
foreach ($dependency in @('pugixml','imgui','cryptopp','xxhash','fmt','texture_decode','agc_texture','gx_frontend','gx_registers','gx_textures','gx_tev','gx_material','gx_geometry','gx_viewport','gx_copy_commands','aurora_input','native_input','virtual_pad')) {
    $archives += "$root/artifacts/runtime-build/libmkw_ps5_$dependency.a"
}
$audio = Get-Content "$root/artifacts/sdl-audio/result.json" -Raw | ConvertFrom-Json
if (!$audio.compiled -or (Get-FileHash $audio.archive).Hash -ne $audio.sha256) { throw 'Rebuild the native SDL audio archive' }
$archives += $audio.archive
$archives += @((Get-ChildItem "$root/artifacts/runtime-build/input/aurora/abseil" -Recurse -File -Filter '*.a').FullName)
foreach ($archive in $archives) { if (!(Test-Path -LiteralPath $archive)) { throw "Missing archive: $archive. Build SDL input and the runtime first." } }
& "$PSScriptRoot/New-VerifiedLibcStub.ps1" -Module "$root/.tools/console/libc.prx" -Objects ($objects + $archives) -Output "$out/libc.a" *> "$out/stub.log"
& "$PSScriptRoot/New-PosixStub.ps1" -Output "$out/posix.o"
@{objects=$objects;archives=$archives;stubs=@("$out/libc.a","$out/posix.o","$out/posix.o.kernel.o")} | ConvertTo-Json -Depth 4 | Set-Content "$out/manifest.json" -Encoding utf8NoBOM
$env:DOTNET_CLI_HOME = "$root/.tools/dotnet-home"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/game_link" -c Release -- "$out/manifest.json" "$out/result.json"
if ($LASTEXITCODE -ne 0) { throw 'Game link audit failed' }
$result = Get-Content "$out/result.json" -Raw | ConvertFrom-Json
$result.unresolved | & "$root/.tools/llvm-mingw-20260908-ucrt-x86_64/bin/llvm-cxxfilt.exe" | Set-Content "$out/unresolved.txt" -Encoding utf8NoBOM
if ($LASTEXITCODE -ne 0) { throw 'Cannot demangle unresolved symbols' }
