[CmdletBinding()]
param(
    # Translated game build tree; artifacts/game-build unless a variant is linked.
    [string]$GameBuild,
    # Retro Rewind product: audits and links the artifacts/game-build-retro tree.
    [switch]$RetroRewind
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if ($RetroRewind -and !$GameBuild) { $GameBuild = "$root/artifacts/game-build-retro" }
$out="$root/artifacts/game-executable/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
New-Item -ItemType Directory -Force "$out/inputs" | Out-Null
$inspect = @{}
if ($GameBuild) { $inspect.GameBuild = $GameBuild }
& "$PSScriptRoot/Inspect-GameLink.ps1" @inspect *> "$out/audit.log"
if($LASTEXITCODE -ne 0){throw 'Game link audit failed'}
$audit=Get-Content "$root/artifacts/game-link/result.json" -Raw | ConvertFrom-Json
if($audit.unresolved.Count -or $audit.skippedMembers.Count){throw 'Game dependencies are incomplete'}
$source=Get-Content "$root/artifacts/game-link/manifest.json" -Raw | ConvertFrom-Json
$manifest=@{objects=@();archives=@();stubs=@()}
$hashes=@();$index=0
foreach($kind in @('objects','archives','stubs')){
    foreach($path in $source.$kind){
        $snapshot="$out/inputs/$index-$(Split-Path $path -Leaf)";$index++
        Copy-Item -LiteralPath $path -Destination $snapshot
        $manifest[$kind]+=$snapshot
        $hashes+=@{source=$path;snapshot=$snapshot;sha256=(Get-FileHash $snapshot).Hash}
    }
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content "$out/manifest.json" -Encoding utf8NoBOM
$hashes | ConvertTo-Json -Depth 5 | Set-Content "$out/inputs.json" -Encoding utf8NoBOM
$env:DOTNET_CLI_HOME="$root/.tools/dotnet-home"
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
& "$root/.tools/dotnet/dotnet.exe" run --project "$PSScriptRoot/tools/game_link" -c Release -- "$out/manifest.json" "$out/result.json" "$out/game.elf" *> "$out/link.log"
if($LASTEXITCODE -ne 0){throw "Game executable link failed; inspect $out/link.log"}
$cli="$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
& "$root/.tools/dotnet/dotnet.exe" $cli self --sign --in "$out/game.elf" --out "$out/eboot.bin" *> "$out/self.log"
if($LASTEXITCODE -ne 0){throw 'Game SELF signature failed'}
& "$root/.tools/dotnet/dotnet.exe" $cli self --inspect --file "$out/eboot.bin" *> "$out/verify-self.log"
if($LASTEXITCODE -ne 0 -or !(Select-String -LiteralPath "$out/verify-self.log" -SimpleMatch 'Integrity: ok')){throw 'Game SELF integrity verification failed'}
@{kind='Mario Kart Wii base runtime executable';linkedGame=$true;nativeExecution=$false;packaged=$false;
    gameElfSha256=(Get-FileHash "$out/game.elf").Hash;ebootSha256=(Get-FileHash "$out/eboot.bin").Hash;
    includedObjects=$audit.includedObjects;unresolved=0;inputManifest="$out/inputs.json"} |
    ConvertTo-Json -Depth 4 | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Actual game ELF and signed SELF built, not packaged or deployed: $out"
