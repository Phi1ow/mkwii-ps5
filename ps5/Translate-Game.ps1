[CmdletBinding()]
param(
    [ValidateRange(1,64)][int]$Threads = 8,
    # Translates the base game with the retro-rewind Code.pul present (required so
    # leaf-inlining/residency decisions respect patched addresses), then runs
    # translate-mod and emits the combined shard graph. Retro-WFC stays off for
    # the first offline bring-up.
    [switch]$RetroRewind
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cli = "$root/Wiicompiled/translator/src/Translator.Cli/bin/Release/net8.0/Translator.Cli.dll"
if (!(Test-Path $cli)) { throw 'Build Translator.Cli in Release first' }
# Use the system .NET 8 runtime; the portable SDK builds the tool on .NET 10.
Push-Location "$root/Wiicompiled"
try {
    & dotnet $cli translate-recursive 0x800060A4 --project projects/mkwii/recomp.yml --threads $Threads --output-metadata generated/base_translation_output.json
    if ($LASTEXITCODE) { throw 'Translation failed' }
    foreach ($command in @('generate-data-init', 'emit-base-manifest')) {
        & dotnet $cli $command --project projects/mkwii/recomp.yml
        if ($LASTEXITCODE) { throw "$command failed" }
    }
    if ($RetroRewind) {
        $modOut = 'build/mods/retro_rewind_full_cpp'
        & dotnet $cli translate-mod --project projects/mkwii/recomp.yml --profile retro-rewind `
            --base-manifest build/base/mkwii_base_manifest.json `
            --skip-retro-wfc --emit-cpp --threads $Threads
        if ($LASTEXITCODE) { throw 'translate-mod failed' }
        & dotnet $cli emit-build-shards --project projects/mkwii/recomp.yml `
            --resolved-profile "$modOut/resolved_dispatch_profile.json" `
            --retro-cpp-dir "$modOut/cpp"
        if ($LASTEXITCODE) { throw 'emit-build-shards failed' }
    } else {
        & dotnet $cli emit-build-shards --project projects/mkwii/recomp.yml
        if ($LASTEXITCODE) { throw 'emit-build-shards failed' }
    }
} finally { Pop-Location }
