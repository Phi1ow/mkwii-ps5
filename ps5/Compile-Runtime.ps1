[CmdletBinding()]
param(
    [string]$CMake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe',
    [string]$Ninja = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = "$root/artifacts/runtime-build"
New-Item -ItemType Directory -Force $build | Out-Null
@{compiled=$false;linkedGame=$false;nativeExecution=$false} | ConvertTo-Json |
    Set-Content "$build/compile-result.json" -Encoding utf8NoBOM
& $CMake -S "$PSScriptRoot" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/cmake/ps5-cxx.cmake" '-DCMAKE_BUILD_TYPE=Release' '-DMKW_PS5_BUILD_CPP_ADAPTER=ON' '-DMKW_PS5_BUILD_RUNTIME_OBJECTS=ON' *> "$root/artifacts/runtime-configure.log"
if ($LASTEXITCODE -ne 0) { throw 'Runtime configure failed; see artifacts/runtime-configure.log' }
& $CMake --build $build --target mkw_ps5_runtime mkw_ps5_runtime_dependencies mkw_ps5_texture_decode mkw_ps5_agc_texture mkw_ps5_gx_frontend mkw_ps5_gx_registers mkw_ps5_gx_textures mkw_ps5_gx_tev mkw_ps5_gx_material mkw_ps5_gx_geometry mkw_ps5_gx_viewport mkw_ps5_gx_copy_commands mkw_ps5_native_input mkw_ps5_virtual_pad mkw_ps5_aurora_input -j 4 -- -k 0 *> "$root/artifacts/runtime-build.log"
if ($LASTEXITCODE -ne 0) { throw 'Runtime compilation failed; see artifacts/runtime-build.log' }
$graph = Get-Content "$build/runtime-graph.json" -Raw | ConvertFrom-Json
$objects = @(Get-ChildItem "$build/CMakeFiles/mkw_ps5_runtime.dir" -Recurse -Filter '*.o')
if ($objects.Count -ne $graph.translationUnits) { throw 'Compiled runtime object count differs from the source graph' }
$gxObjects = @(Get-ChildItem "$build/CMakeFiles/mkw_ps5_gx_frontend.dir" -Recurse -Filter '*.o')
if ($gxObjects.Count -ne 19) { throw 'Expected 17 GX frontend units plus two Aurora platform service units' }
$registerObjects = @(Get-ChildItem "$build/CMakeFiles/mkw_ps5_gx_registers.dir" -Recurse -Filter '*.o')
if ($registerObjects.Count -ne 3) { throw 'Expected the three GX register translation units' }
$archives = @('imgui', 'xxhash', 'pugixml', 'cryptopp', 'fmt', 'texture_decode', 'agc_texture', 'gx_frontend', 'gx_registers', 'gx_textures', 'gx_tev','gx_material','gx_geometry','gx_viewport','gx_copy_commands','native_input','virtual_pad','aurora_input') | ForEach-Object {
    $file = Get-Item "$build/libmkw_ps5_$_.a"
    if ($file.Length -eq 0) { throw "Empty dependency archive: $_" }
    @{name=$file.Name;bytes=$file.Length;sha256=(Get-FileHash $file.FullName).Hash}
}
$archives += @(Get-ChildItem "$build/input/aurora/abseil" -Recurse -File -Filter '*.a' | ForEach-Object {
    @{name=$_.Name;path=$_.FullName;bytes=$_.Length;sha256=(Get-FileHash $_.FullName).Hash}
})
@{compiled=$true;translationUnits=$objects.Count;gxFrontendTranslationUnits=$gxObjects.Count;gxRegisterTranslationUnits=$registerObjects.Count;archives=@($archives);linkedGame=$false;nativeExecution=$false} |
    ConvertTo-Json -Depth 5 | Set-Content "$build/compile-result.json" -Encoding utf8NoBOM
Write-Host 'All base-game runtime sources compile for PS5. Linking still requires their external dependencies and platform backends.'
