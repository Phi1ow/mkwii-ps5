[CmdletBinding()]
param([Parameter(Mandatory)][string]$ExecutableDirectory,
    [Parameter(Mandatory)][ValidatePattern('^PPSA996[0-9]{2}$')][string]$TitleId,
    # Name shown on the PS5 home screen; the development name by default.
    [string]$TitleName,
    # Folder receiving <TitleId>/ and build.json; a timestamped artifacts/game-package folder by default.
    [string]$OutputDirectory,
    # Retro Rewind product: embeds the RetroRewind6 asset tree at /app0/RetroRewind6
    # and points the runtime's Riivolution overlay at it.
    [switch]$RetroRewind,
    # Location of the extracted RetroRewind6 pack (contains Binaries/, xml/, Tracks/...).
    [string]$RetroRewindPack)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=(Resolve-Path -LiteralPath $ExecutableDirectory).Path
$built=Get-Content "$exe/build.json" -Raw | ConvertFrom-Json
if(!$built.linkedGame -or (Get-FileHash "$exe/eboot.bin").Hash -ne $built.ebootSha256){throw 'Game executable manifest mismatch'}
$out=if($OutputDirectory){$OutputDirectory}else{"$root/artifacts/game-package/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"}
if(Test-Path -LiteralPath "$out/$TitleId"){throw "Refusing to overwrite an existing package: $out/$TitleId"}
$package="$out/$TitleId"
foreach($folder in @('sce_sys','sce_module','shaders','runtime/assets/dsp','UserData/NAND')){
    New-Item -ItemType Directory -Force "$package/$folder" | Out-Null
}
$inputs=@{
    'eboot.bin'="$exe/eboot.bin"
    'sce_module/libc.prx'="$root/.tools/console/libc.prx"
    'sce_sys/icon0.png'="$root/SharpProspero/samples/prospero-3d/sce_sys/icon0.png"
    'shaders/gx_vertex.sb'="$root/artifacts/gx-varyings/vertex.sb"
    'shaders/gx_pixel.sb'="$root/artifacts/tev-fragment/tev_probe.sb"
    'shaders/copy_vertex.sb'="$root/ps5link-sdk/shaders/third_party/sharpprospero/mesh_vs.sb"
    'shaders/copy_pixel.sb'="$root/artifacts/gx-copy-shader/copy.sb"
    'runtime/assets/dsp/dsp_coef.bin'="$root/Wiicompiled/runtime/assets/dsp/dsp_coef.bin"
}
foreach($name in $inputs.Keys){Copy-Item -LiteralPath $inputs[$name] -Destination "$package/$name"}
Copy-Item -LiteralPath "$root/Wiicompiled/runtime/assets/wii" -Destination "$package/wii_bootstrap" -Recurse
$param=Get-Content "$root/SharpProspero/samples/prospero-3d/sce_sys/param.json" -Raw | ConvertFrom-Json
$param.titleId=$TitleId;$param.conceptId=$TitleId.Substring(4)
$param.contentId="IV0000-${TitleId}_00-WIICOMPILEDMKW01"
$param.localizedParameters.'en-US'.titleName=if($TitleName){$TitleName}else{"Mario Kart Wii - PS5 development $TitleId"}
$param | ConvertTo-Json -Depth 8 | Set-Content "$package/sce_sys/param.json" -Encoding utf8NoBOM
'WiiCompiled PS5 portable development package' | Set-Content "$package/portable.txt" -Encoding utf8NoBOM
'WiiCompiled stores this development title''s Wii save data in this directory.' |
    Set-Content "$package/UserData/NAND/README.txt" -Encoding utf8NoBOM
$configPaths = @"
dvd_root = "/app0/DATA"
nand_root = "/app0/UserData/NAND"
"@
if ($RetroRewind) {
    # The generated mod manifest registers the "RetroRewind6" overlay root
    # relative to /app0; declaring it explicitly keeps the root visible even if
    # that registration ordering changes. The pack's XML then maps
    # /RetroRewind6/... externals onto the disc filesystem.
    if (!$RetroRewindPack) {
        $RetroRewindPack = "$root/Wiicompiled/PulsarPacks/completed/RetroRewind/RetroRewind6"
    }
    if (!(Test-Path -LiteralPath "$RetroRewindPack/Binaries/Code.pul")) {
        throw "RetroRewind6 pack not found at $RetroRewindPack (expected Binaries/Code.pul)"
    }
    robocopy "$RetroRewindPack" "$package/RetroRewind6" /E /NFL /NDL /NJH /NP | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "RetroRewind6 asset copy failed (robocopy exit $LASTEXITCODE)" }
    $configPaths += "`nretro_rewind_root = `"/app0/RetroRewind6`""
}
@"
[video]
graphics_api = "agc"
display_mode = "borderless"
resolution_multiplier = 1.0
frame_interpolation_fps = 0
texture_replacements = false
texture_dumps = false
disable_copy_filter = true

[network]
enabled = true

[discord]
enabled = false

[paths]
$configPaths
"@ | Set-Content "$package/UserData/Config.toml" -Encoding utf8NoBOM
$cli="$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
& "$root/.tools/dotnet/dotnet.exe" $cli param --folder $package *> "$out/param.log"
if($LASTEXITCODE -ne 0){throw 'Game parameter validation failed'}
& "$root/.tools/dotnet/dotnet.exe" $cli sysver --folder $package --policy match --apply *> "$out/sysver.log"
if($LASTEXITCODE -ne 0){throw 'Game firmware declaration failed'}
$files=@{}
foreach($file in Get-ChildItem $package -Recurse -File){
    $relative=[IO.Path]::GetRelativePath($package,$file.FullName).Replace('\','/')
    $files[$relative]=(Get-FileHash $file.FullName).Hash
}
@{kind='Mario Kart Wii development package';titleId=$TitleId;completePackage=$true;
    gamePlayable=$false;linkedGame=$true;hardwareVerified=$false;auroraBootstrap=$true;
    requiredGameData="/data/$TitleId/DATA";runtimeGameData='/app0/DATA';
    executableBuild=$exe;ebootSha256=$built.ebootSha256;files=$files} |
    ConvertTo-Json -Depth 5 | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Game package prepared; DATA must be placed inside this title before launch: $out"
