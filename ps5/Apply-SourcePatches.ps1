[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$patches = @(
    @{repo='Wiicompiled';name='wiicompiled-platform-api.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-memory-policy.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-runtime.patch'},
    @{repo='Wiicompiled';name='wiicompiled-texture-data.patch'},
    @{repo='Wiicompiled';name='wiicompiled-gx-frontend.patch'},
    @{repo='Wiicompiled';name='wiicompiled-gx-registers.patch'},
    @{repo='Wiicompiled';name='wiicompiled-gx-memory-sources.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-absolute-paths.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-constructor-faults.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-platform-services.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-direct-log.patch'},
    @{repo='Wiicompiled';name='wiicompiled-audio-ownership.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-nand-publication.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-optional-overlay.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-perf-counters.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-resolved-ranges.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-native-site.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-dl-exception-log.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-function-profile.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-checked-psq-deferred-pages.patch'},
    @{repo='Wiicompiled';name='wiicompiled-ps5-dispatch-registration-log.patch'},
    @{repo='SharpProspero';name='sharpprospero-comdat-frames.patch'},
    @{repo='SharpProspero';name='sharpprospero-pthread-overrides.patch'},
    @{repo='SharpProspero';name='sharpprospero-filesystem-overrides.patch'},
    @{repo='SharpProspero';name='sharpprospero-posix-error-strings.patch'},
    @{repo='SharpProspero';name='sharpprospero-clock-override.patch'}
)
foreach ($entry in $patches) {
    $repo = "$root/$($entry.repo)"
    $name = $entry.name
    $patch = "$PSScriptRoot/patches/$name"
    # A reverse dry run detects an already-applied patch without undoing it.
    & git -C $repo apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) { Write-Host "Already applied: $name"; continue }
    & git -C $repo apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw "Patch $name does not apply; inspect source changes before proceeding" }
    & git -C $repo apply $patch
    if ($LASTEXITCODE -ne 0) { throw "Patch $name failed" }
    Write-Host "Applied: $name"
}
