[CmdletBinding()]
param(
    # Receives <TitleId>/, README.md and outils/; must not already hold a package.
    [string]$OutputDirectory,
    [ValidatePattern('^PPSA996[0-9]{2}$')][string]$TitleId = 'PPSA99611',
    [string]$TitleName = 'Kart PS5',
    [string]$CMake = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
)
# Player package: the runtime archives are rebuilt with MKW_PS5_RELEASE=ON
# (no autopilot, microbench, sampler, placement, screenshots or periodic
# [mkw-frame-stats] line), the game is linked against artifacts/game-build,
# then the development archives are rebuilt so later dev builds are unchanged.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) { $OutputDirectory = "$root/test-final" }
if (Test-Path -LiteralPath "$OutputDirectory/$TitleId") { throw "Move the existing package away first: $OutputDirectory/$TitleId" }
$build = "$root/artifacts/runtime-build"
$targets = @('mkw_ps5_runtime', 'mkw_ps5_runtime_dependencies', 'mkw_ps5_texture_decode', 'mkw_ps5_agc_texture', 'mkw_ps5_gx_frontend',
    'mkw_ps5_gx_registers', 'mkw_ps5_gx_textures', 'mkw_ps5_gx_tev', 'mkw_ps5_gx_material', 'mkw_ps5_gx_geometry', 'mkw_ps5_gx_viewport',
    'mkw_ps5_gx_copy_commands', 'mkw_ps5_native_input', 'mkw_ps5_virtual_pad', 'mkw_ps5_aurora_input')
$logs = "$root/artifacts/release-build"
New-Item -ItemType Directory -Force $logs | Out-Null
# Reconfigure with the source path spelling the tree was generated with: the
# workspace is reachable through a junction, and a different spelling moves every
# out-of-tree object (Wiicompiled, Aurora) to a second directory, which doubles the
# runtime objects Inspect-GameLink collects. CMakeCache keeps its first spelling;
# Ninja's regeneration rule holds the one that generated the object paths.
$rerun = Select-String -LiteralPath "$build/CMakeFiles/rules.ninja" -Pattern '--regenerate-during-build -S"([^"]+)"' | Select-Object -First 1
if (!$rerun) { throw 'Configure artifacts/runtime-build with Compile-Runtime.ps1 first' }
$source = $rerun.Matches[0].Groups[1].Value
function Build-Runtime([string]$release, [string]$log) {
    & $CMake -S $source -B $build "-DMKW_PS5_RELEASE=$release" *> "$logs/configure-$log.log"
    if ($LASTEXITCODE -ne 0) { throw "Runtime configure ($log) failed; see $logs/configure-$log.log" }
    & $CMake --build $build --target @targets -j 4 -- -k 0 *> "$logs/build-$log.log"
    if ($LASTEXITCODE -ne 0) { throw "Runtime build ($log) failed; see $logs/build-$log.log" }
}
$executable = $null
try {
    Build-Runtime 'ON' 'release'
    $before = @(Get-ChildItem "$root/artifacts/game-executable" -Directory).Name
    & "$PSScriptRoot/Build-GameExecutable.ps1"
    $executable = (Get-ChildItem "$root/artifacts/game-executable" -Directory | Where-Object { $before -notcontains $_.Name } |
        Sort-Object Name | Select-Object -Last 1).FullName
    if (!$executable) { throw 'Build-GameExecutable produced no new folder' }
} finally {
    # Always return the shared runtime tree to the development configuration.
    Build-Runtime 'OFF' 'development'
}
# The release define must have removed every flag-file probe and the report line.
$elf = [IO.File]::ReadAllBytes("$executable/game.elf")
$text = [Text.Encoding]::ASCII.GetString($elf)
foreach ($marker in @('autopilot.txt', 'microbench.txt', 'profile.txt', 'placement.txt', 'screenshot.txt', '[mkw-frame-stats]')) {
    if ($text.Contains($marker)) { throw "Release executable still contains '$marker': $executable" }
}
@{release = $true; removedDiagnostics = @('autopilot', 'microbench', 'guest sampler', 'thread placement', 'screenshots', 'frame-stats report')} |
    ConvertTo-Json | Set-Content "$executable/release.json" -Encoding utf8NoBOM
& "$PSScriptRoot/Package-Game.ps1" -ExecutableDirectory $executable -TitleId $TitleId -TitleName $TitleName -OutputDirectory $OutputDirectory
# Package evidence (hashes, local paths, validation logs) stays with the executable.
New-Item -ItemType Directory -Force "$executable/package" | Out-Null
foreach ($name in @('build.json', 'param.log', 'sysver.log')) {
    Move-Item -LiteralPath "$OutputDirectory/$name" -Destination "$executable/package/$name"
}
# Scoped permission payload: chmod 0755 of this title's eboot.bin and libc.prx only.
& "$PSScriptRoot/Build-FileAccessDiagnostic.ps1" -TitleId $TitleId -RepairExecute
$payload = (Get-ChildItem "$root/artifacts/file-access" -Directory | Sort-Object Name | Select-Object -Last 1).FullName
New-Item -ItemType Directory -Force "$OutputDirectory/outils" | Out-Null
Copy-Item -LiteralPath "$payload/diagnostic.elf" -Destination "$OutputDirectory/outils/droits-kart-ps5.elf"
Copy-Item -LiteralPath "$PSScriptRoot/release/README-test-final.md" -Destination "$OutputDirectory/README.md"
Write-Host "Player package ready: $OutputDirectory (executable $executable)"
