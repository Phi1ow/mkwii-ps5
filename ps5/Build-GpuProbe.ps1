[CmdletBinding()]
param(
    [string]$Clang,
    [string]$Dotnet,
    [string]$LibcFile,
    [string]$PixelShaderFile,
    [string]$VertexShaderFile,
    [string]$CopyShaderFile,
    [switch]$TextureDraw,
    [switch]$FramebufferSample,
    [switch]$OwnedColorTargets,
    [switch]$GxCopyBinding,
    [switch]$InspectFence,
    [switch]$GpuFence,
    [switch]$GpuBlit,
    [switch]$CopyFormats,
    [switch]$CopyReadback,
    [switch]$GxCopyExecute,
    [switch]$InspectDepthDefaults,
    [switch]$DepthTest,
    [switch]$GxDraw,
    [switch]$GxDisplayCopy,
    [switch]$GxPresent,
    [switch]$GxLifecycle,
    [switch]$OwnedShaders,
    [switch]$AuroraBootstrap,
    [switch]$NativeInput,
    [switch]$SdlInput,
    [switch]$SdlAudio,
    [switch]$AuroraPad,
    [switch]$InspectSdlImports,
    [switch]$FileSystem,
    [switch]$PlatformServices,
    [switch]$WiiTextureDecode,
    [switch]$TextureUpload,
    [switch]$TevArithmetic,
    [switch]$TevProgram,
    [switch]$TevDirect,
    [switch]$GxGeometry,
    [switch]$RawVertex,
    [switch]$GxTransforms,
    [switch]$CurrentMatrix,
    [switch]$GxLighting,
    [switch]$GxTexgen,
    [switch]$InspectLinkage,
    [switch]$GxVaryings,
    [switch]$FragmentOutput,
    [switch]$GxBlend,
    [switch]$GxViewport,
    [switch]$Fp32Output,
    [switch]$Unorm16Output,
    [ValidateSet('ps5link', 'SharpProspero')][string]$Linker = 'ps5link',
    [ValidateSet('AGC', 'Memory', 'Cpp', 'Runtime')][string]$ProbeKind = 'AGC',
    [ValidatePattern('^[A-Z]{4}[0-9]{5}$')][string]$TitleId = 'WIKT00001'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path $PSScriptRoot -Parent
if (!$Clang) { $Clang = (Get-ChildItem "$root/.tools/llvm-mingw-*/bin/clang.exe" | Select-Object -First 1).FullName }
if (!$Dotnet) { $Dotnet = "$root/.tools/dotnet/dotnet.exe" }
foreach ($file in @($Clang, $Dotnet)) {
    if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing tool: $file" }
}
function Run([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed (exit $LASTEXITCODE)" }
}
$env:DOTNET_CLI_HOME = "$root/.tools/dotnet-home"
$env:NUGET_PACKAGES = "$root/.tools/nuget"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
# Unique build directories prevent a failed link/sign from publishing an old binary.
$out = "$root/artifacts/gpu-probe/$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
$package = "$out/$TitleId"
New-Item -ItemType Directory -Force "$package/sce_sys", "$package/sce_module" | Out-Null
$sdk = "$root/ps5link-sdk"
foreach ($stage in @('vs', 'ps')) {
    $name = "mesh_${stage}_sb"
    $shaderPath="$sdk/shaders/third_party/sharpprospero/mesh_$stage.sb"
    if ($stage -eq 'vs' -and $VertexShaderFile) {
        if (!$TextureDraw -or $ProbeKind -ne 'Runtime') { throw 'Custom vertex shader requires the Runtime texture diagnostic' }
        $vertexVariant = if ($GxVaryings) { 'gx-varyings' } elseif ($GxTexgen) { 'gx-texgen' } elseif ($GxLighting) { 'gx-lighting' } elseif ($GxTransforms) { 'gx-transforms' } elseif ($RawVertex) { 'gx-vertex-raw' } else { 'gx-vertex' }
        $vertexSource = if ($RawVertex) { 'gx_vertex_raw.c' } else { 'gx_vertex_probe.c' }
        $vertexBuild=Get-Content "$root/artifacts/$vertexVariant/build.json" -Raw | ConvertFrom-Json
        if ($vertexBuild.containerSha256 -ne (Get-FileHash -LiteralPath $VertexShaderFile).Hash -or
            $vertexBuild.headerReaderSha256 -ne (Get-FileHash "$PSScriptRoot/tools/agc_header.py").Hash -or
            $vertexBuild.sourceSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/shaders/$vertexSource").Hash) { throw 'Stale vertex shader; rerun build_vertex_shader.py' }
        if ($RawVertex -and (!$vertexBuild.rawGeometry -or $vertexBuild.geometryAbiSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/gx_vertex_format.h").Hash)) { throw 'Stale raw geometry shader ABI' }
        if ($GxTransforms -and (!$vertexBuild.gxTransforms -or $vertexBuild.transformAbiSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/gx_transform_format.h").Hash)) { throw 'Stale transform shader ABI' }
        if ($GxLighting -and (!$vertexBuild.gxLighting -or $vertexBuild.lightingSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/gx_lighting.h").Hash)) { throw 'Stale lighting shader' }
        if ($GxTexgen -and (!$vertexBuild.gxTexgen -or $vertexBuild.texgenSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/gx_texgen.h").Hash)) { throw 'Stale texgen shader' }
        if ($GxVaryings -and (!$vertexBuild.gxVaryings -or $vertexBuild.varyingHeaderSha256 -ne (Get-FileHash "$PSScriptRoot/tools/agc_varyings.py").Hash)) { throw 'Stale vertex varying interface' }
        $shaderPath=(Resolve-Path -LiteralPath $VertexShaderFile).Path
    }
    $bytes = [IO.File]::ReadAllBytes($shaderPath)
    $body = ($bytes | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
    [IO.File]::WriteAllText("$out/$name.h", "static const unsigned char $name[] = {$body};`nstatic const unsigned int ${name}_len = $($bytes.Length);`n")
}
$libNames = @('elf_object', 'linker', 'dynwriter', 'catalog', 'catalog_extra', 'catalog_lookup', 'nid', 'sha1')
$libs = @($libNames | ForEach-Object { "$sdk/linker/$_.c" })
Run $Clang (@('-O2', '-Wall', '-o', "$out/link_real.exe", "$sdk/linker/link_real.c") + $libs)
# Freestanding source declares its ABI explicitly and includes no OS headers.
# FreeBSD x86-64 produces the SysV/LP64 ELF objects consumed by ps5link.
# This does not replace a PS5 C++ sysroot for the full game.
$target = @('--no-default-config', '--target=x86_64-unknown-freebsd', '-ffreestanding', '-fno-stack-protector', '-fPIC', '-O2')
Run $Clang ($target + @('-c', "$sdk/linker/crt1.S", '-o', "$out/crt1.o"))
$probeFlags = @()
if ($TevProgram) { $TevArithmetic = [switch]$true }
if ($GxGeometry -and !$TevDirect) { throw 'GxGeometry requires TevDirect' }
if ($GxGeometry) { $probeFlags += '-DMKW_GEOMETRY_PROBE' }
if ($RawVertex -and (!$GxGeometry -or !$VertexShaderFile)) { throw 'RawVertex requires GxGeometry and the raw vertex shader' }
if ($RawVertex) { $probeFlags += '-DMKW_RAW_VERTEX' }
if ($GxTransforms -and !$RawVertex) { throw 'GxTransforms requires RawVertex' }
if ($GxTransforms) { $probeFlags += '-DMKW_GX_TRANSFORMS' }
if ($CurrentMatrix -and !$GxTransforms) { throw 'CurrentMatrix requires GxTransforms' }
if ($GxLighting -and !$CurrentMatrix) { throw 'This lighting diagnostic requires CurrentMatrix' }
if ($GxLighting) { $probeFlags += '-DMKW_GX_LIGHTING' }
if ($GxTexgen -and !$GxLighting) { throw 'This texgen diagnostic requires GxLighting' }
if ($GxTexgen) { $probeFlags += '-DMKW_GX_TEXGEN' }
if ($GxVaryings -and (!$GxTexgen -or $InspectLinkage)) { throw 'GxVaryings requires GxTexgen and cannot inspect the original two-parameter interface' }
if ($GxVaryings) { $probeFlags += '-DMKW_GX_VARYINGS' }
if ($FragmentOutput -and !$GxVaryings) { throw 'FragmentOutput requires GxVaryings' }
if ($FragmentOutput) { $probeFlags += '-DMKW_GX_FRAGMENT_OUTPUT' }
if ($GxViewport -and !$GxBlend) { throw 'GxViewport diagnostic requires GxBlend' }
if ($GxViewport) { $probeFlags += '-DMKW_GX_VIEWPORT' }
if ($GxBlend -and !$FragmentOutput) { throw 'GxBlend requires FragmentOutput' }
if ($GxBlend) { $probeFlags += '-DMKW_GX_BLEND' }
if ($Fp32Output -and !$FragmentOutput) { throw 'Fp32Output requires FragmentOutput' }
if ($Fp32Output) { $probeFlags += '-DMKW_GX_FP32_OUTPUT' }
if ($Unorm16Output -and (!$FragmentOutput -or $Fp32Output)) { throw 'Unorm16Output requires FragmentOutput without Fp32Output' }
if ($Unorm16Output) { $probeFlags += '-DMKW_GX_UNORM16_OUTPUT' }
if ($FramebufferSample) {
    if (!$TextureDraw -or $TevDirect -or $TevArithmetic -or $WiiTextureDecode -or $TextureUpload -or $VertexShaderFile -or $InspectLinkage) { throw 'FramebufferSample requires only the plain Runtime TextureDraw diagnostic' }
    $probeFlags += '-DMKW_FRAMEBUFFER_SAMPLE'
}
if ($OwnedColorTargets -and !$FramebufferSample) { throw 'OwnedColorTargets requires FramebufferSample' }
if ($OwnedColorTargets) { $probeFlags += '-DMKW_OWNED_COLOR_TARGETS' }
if ($GxCopyBinding -and !$OwnedColorTargets) { throw 'GxCopyBinding requires OwnedColorTargets' }
if ($GxCopyBinding) { $probeFlags += '-DMKW_GX_COPY_BINDING' }
if ($InspectFence -and !$FramebufferSample) { throw 'InspectFence requires FramebufferSample' }
if ($InspectFence) { $probeFlags += '-DMKW_INSPECT_FENCE' }
if ($GpuFence -and (!$OwnedColorTargets -or $InspectFence)) { throw 'GpuFence requires OwnedColorTargets without InspectFence' }
if ($GpuFence) { $probeFlags += '-DMKW_GPU_FENCE' }
if ($GpuBlit -and (!$GpuFence -or !$GxCopyBinding)) { throw 'GpuBlit requires GpuFence and GxCopyBinding' }
if ($GpuBlit) { $probeFlags += '-DMKW_GPU_BLIT' }
if($CopyFormats -and !$CopyShaderFile){throw 'CopyFormats requires CopyShaderFile'}
if($CopyFormats){$probeFlags+='-DMKW_GX_COPY_FORMATS'}
if($CopyReadback -and (!$CopyShaderFile -or $CopyFormats)){throw 'CopyReadback requires CopyShaderFile without CopyFormats'}
if($CopyReadback){$probeFlags+='-DMKW_GX_COPY_READBACK'}
if($GxCopyExecute -and !$CopyReadback){throw 'GxCopyExecute requires CopyReadback'}
if($InspectDepthDefaults -and !$FramebufferSample){throw 'InspectDepthDefaults requires FramebufferSample'}
if($DepthTest -and (!$GpuBlit -or !$GxCopyExecute)){throw 'DepthTest requires GpuBlit and GxCopyExecute'}
if($GxDraw -and (!$GxVaryings -or !$FragmentOutput)){throw 'GxDraw requires the full raw GX varying and fragment shaders'}
if($GxDraw){$probeFlags+='-DMKW_GX_DRAW_PROBE'}
if($GxDisplayCopy -and (!$GxDraw -or !$CopyShaderFile)){throw 'GxDisplayCopy requires GxDraw and CopyShaderFile'}
if($GxPresent -and !$GxDisplayCopy){throw 'GxPresent requires GxDisplayCopy'}
if($GxPresent){$probeFlags+='-DMKW_GX_PRESENT'}
if($GxLifecycle -and !$GxPresent){throw 'GxLifecycle requires GxPresent'}
if($GxLifecycle){$probeFlags+='-DMKW_GX_LIFECYCLE'}
if($OwnedShaders -and !$GxLifecycle){throw 'OwnedShaders requires GxLifecycle'}
if($OwnedShaders){$probeFlags+='-DMKW_OWNED_SHADERS'}
if($AuroraBootstrap -and !$GxLifecycle){throw 'AuroraBootstrap requires GxLifecycle'}
if($AuroraBootstrap){$probeFlags+='-DMKW_AURORA_BOOTSTRAP'}
if($NativeInput -and ($ProbeKind -ne 'Runtime' -or $TextureDraw -or $AuroraBootstrap)){throw 'NativeInput requires Runtime without texture/bootstrap probes'}
if($NativeInput){$probeFlags+='-DMKW_NATIVE_INPUT'}
if($SdlInput -and ($ProbeKind -ne 'Runtime' -or $TextureDraw -or $AuroraBootstrap -or $NativeInput)){throw 'SdlInput requires Runtime without other rendering/input probes'}
if($SdlInput){$probeFlags+='-DMKW_SDL_INPUT'}
if($SdlAudio -and ($ProbeKind -ne 'Runtime' -or $TextureDraw -or $AuroraBootstrap -or $NativeInput -or $SdlInput -or $FileSystem -or $PlatformServices)){throw 'SdlAudio requires an isolated Runtime probe'}
if($SdlAudio){$probeFlags+='-DMKW_SDL_AUDIO'}
if($InspectSdlImports -and !$SdlInput){throw 'InspectSdlImports requires SdlInput'}
if($FileSystem -and ($ProbeKind -ne 'Runtime' -or $TextureDraw -or $AuroraBootstrap -or $NativeInput -or $SdlInput)){throw 'FileSystem requires Runtime without other rendering/input probes'}
if($FileSystem){$probeFlags+='-DMKW_FILESYSTEM_PROBE'}
if($PlatformServices -and ($ProbeKind -ne 'Runtime' -or $TextureDraw -or $SdlInput -or $NativeInput -or $FileSystem)){throw 'PlatformServices requires an isolated Runtime probe'}
if($PlatformServices){$probeFlags+='-DMKW_PLATFORM_SERVICES_PROBE'}
if($AuroraPad -and (!$SdlInput -or $InspectSdlImports)){throw 'AuroraPad requires functional SdlInput'}
if($GxDisplayCopy){
    $probeFlags+='-DMKW_GX_DISPLAY_COPY'
    $meshBytes=[IO.File]::ReadAllBytes("$sdk/shaders/third_party/sharpprospero/mesh_vs.sb")
    $meshBody=($meshBytes|ForEach-Object{'0x{0:x2}' -f $_}) -join ','
    [IO.File]::WriteAllText("$out/display_mesh_sb.h","static const unsigned char display_mesh_sb[] = {$meshBody};`nstatic const unsigned int display_mesh_sb_len = $($meshBytes.Length);`n")
}
if($GxCopyExecute){$probeFlags+='-DMKW_GX_COPY_EXECUTE'}
if($GxCopyExecute -or $GxDraw){
    & "$PSScriptRoot/Compile-Runtime.ps1"
}
if ($CopyShaderFile) {
    if (!$GpuBlit -and !$GxDisplayCopy) { throw 'CopyShaderFile requires GpuBlit or GxDisplayCopy' }
    $copyBuild=Get-Content "$root/artifacts/gx-copy-shader/build.json" -Raw | ConvertFrom-Json
    if ($copyBuild.containerSha256 -ne (Get-FileHash -LiteralPath $CopyShaderFile).Hash) { throw 'Stale GX copy shader' }
    foreach($item in $copyBuild.sources.PSObject.Properties) {
        if ($item.Value -ne (Get-FileHash -LiteralPath "$root/$($item.Name)").Hash) { throw "Stale GX copy shader source: $($item.Name)" }
    }
    $copyBytes=[IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $CopyShaderFile).Path)
    $copyBody=($copyBytes | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
    [IO.File]::WriteAllText("$out/copy_shader_sb.h","static const unsigned char copy_shader_sb[] = {$copyBody};`nstatic const unsigned int copy_shader_sb_len = $($copyBytes.Length);`n")
    if($GpuBlit){$probeFlags += '-DMKW_GX_FILTERED_COPY'}
}
if ($FramebufferSample) {
    $shaderBuild=Get-Content "$root/artifacts/agc-shaders/build.json" -Raw | ConvertFrom-Json
    if (!$PixelShaderFile -or $shaderBuild.containerSha256 -ne (Get-FileHash -LiteralPath $PixelShaderFile).Hash -or
        $shaderBuild.sourceSha256 -ne (Get-FileHash "$sdk/shaders/src/textured_p.s").Hash) {
        throw 'FramebufferSample requires the final validated textured_p.sb, not the intermediate texture_container.sb'
    }
}
if ($TevDirect -and (!$TextureDraw -or $TevArithmetic -or $WiiTextureDecode -or $TextureUpload)) { throw 'TevDirect requires TextureDraw without other texture/TEV probe modes' }
if ($WiiTextureDecode -and !$TextureDraw) { throw 'WiiTextureDecode requires TextureDraw' }
if ($TextureUpload -and !$WiiTextureDecode) { throw 'TextureUpload requires WiiTextureDecode' }
if ($TevArithmetic -and (!$TextureDraw -or $WiiTextureDecode -or $TextureUpload)) { throw 'TevArithmetic requires TextureDraw without texture conversion/upload' }
if ($TextureDraw) {
    if (!$PixelShaderFile -or $ProbeKind -ne 'Runtime') { throw 'TextureDraw requires Runtime and PixelShaderFile' }
    $fixtureArgs = @('run', '--project', "$PSScriptRoot/tools/agc_fixture", '-c', 'Release', '--', "$out/texture_fixture.h")
    if ($TevDirect) {
        $pixelVariant=if ($Unorm16Output) { 'tev-fragment-unorm16' } elseif ($Fp32Output) { 'tev-fragment-fp32' } elseif ($FragmentOutput) { 'tev-fragment' } elseif ($GxVaryings) { 'tev-varyings' } elseif ($GxTexgen) { 'tev-projected' } else { 'tev-direct' }
        $directBuild=Get-Content "$root/artifacts/$pixelVariant/build.json" -Raw | ConvertFrom-Json
        if ($GxTexgen -and !$directBuild.projectedCoordinates) { throw 'Texgen needs the projected-coordinate pixel shader' }
        if ($GxVaryings -and (!$directBuild.gxVaryings -or $directBuild.varyingHeaderSha256 -ne (Get-FileHash "$PSScriptRoot/tools/agc_varyings.py").Hash)) { throw 'Stale pixel varying interface' }
        if ($FragmentOutput -and (!$directBuild.fragmentOutput -or !$directBuild.alphaDiscard)) { throw 'FragmentOutput requires actual alpha discard and RGBA output' }
        if ($Fp32Output -and !$directBuild.fp32Output) { throw 'Fp32Output requires the uncompressed fragment export shader' }
        if ($Unorm16Output -and !$directBuild.unorm16Output) { throw 'Unorm16Output requires the corresponding fragment export shader' }
        if (!$directBuild.directMaterial -or $directBuild.containerSha256 -ne (Get-FileHash $PixelShaderFile).Hash -or
            $directBuild.headerReaderSha256 -ne (Get-FileHash "$PSScriptRoot/tools/agc_header.py").Hash -or
            $directBuild.sourceSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/shaders/tev_direct_probe.c").Hash -or
            $directBuild.stageSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/tev_stage.h").Hash -or
            $directBuild.arithmeticSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/tev_integer.h").Hash) { throw 'Stale direct TEV shader; rerun build_tev_shader.py --direct' }
        $probeFlags += '-DMKW_TEV_DIRECT'
    }
    if ($TevArithmetic) {
        $tevDir = $(if ($TevProgram) { "$root/artifacts/tev-program" } else { "$root/artifacts/tev-shaders" })
        $tevSource = $(if ($TevProgram) { "$PSScriptRoot/gpu/shaders/tev_program_probe.c" } else { "$PSScriptRoot/gpu/shaders/tev_probe.c" })
        $tevHost = Get-Content "$tevDir/host-result.json" -Raw | ConvertFrom-Json
        $tevBuild = Get-Content "$tevDir/build.json" -Raw | ConvertFrom-Json
        $tevHash = (Get-FileHash "$PSScriptRoot/gpu/tev_integer.h").Hash
        if ($tevHost.arithmeticSha256 -ne $tevHash -or $tevBuild.arithmeticSha256 -ne $tevHash -or
            $tevBuild.headerReaderSha256 -ne (Get-FileHash "$PSScriptRoot/tools/agc_header.py").Hash -or
            $tevBuild.sourceSha256 -ne (Get-FileHash $tevSource).Hash -or
            $tevBuild.containerSha256 -ne (Get-FileHash -LiteralPath $PixelShaderFile).Hash -or
            $tevHost.casesSha256 -ne (Get-FileHash "$tevDir/cases.json").Hash) {
            throw 'Stale TEV shader/cases; rerun test_tev_integer.py and build_tev_shader.py'
        }
        $fixtureArgs += "$tevDir/cases.json"
        $probeFlags += '-DMKW_TEV_ARITHMETIC'
        if ($TevProgram) {
            $stageHash=(Get-FileHash "$PSScriptRoot/gpu/tev_stage.h").Hash
            if ($tevBuild.stageSha256 -ne $stageHash -or $tevHost.stageSha256 -ne $stageHash -or
                $tevHost.programEncoderSha256 -ne (Get-FileHash "$PSScriptRoot/gpu/gx_tev_program.cpp").Hash -or
                $tevHost.programsSha256 -ne (Get-FileHash "$tevDir/programs.bin").Hash) { throw 'Stale TEV program fixture or shader' }
            $fixtureArgs += "$tevDir/programs.bin"
            $probeFlags += '-DMKW_TEV_PROGRAM'
        }
    }
    Run $Dotnet $fixtureArgs
    $probeFlags += '-DMKW_TEXTURE_DRAW_PROBE'
}
if ($PixelShaderFile) {
    if ($ProbeKind -ne 'Runtime') { throw 'Custom shader validation requires the Runtime diagnostic log' }
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $PixelShaderFile).Path)
    $body = ($bytes | ForEach-Object { '0x{0:x2}' -f $_ }) -join ','
    [IO.File]::WriteAllText("$out/pixel_shader_sb.h", "static const unsigned char pixel_shader_sb[] = {$body};`nstatic const unsigned int pixel_shader_sb_len = $($bytes.Length);`n")
    $probeFlags += '-DMKW_PIXEL_SHADER_PROBE'
}
$extraObjects = @()
if ($InspectLinkage) {
    if (!$TextureDraw -or $ProbeKind -ne 'Runtime') { throw 'InspectLinkage requires a Runtime texture draw' }
    $probeFlags += '-DMKW_INSPECT_LINKAGE'
}
if ($ProbeKind -in @('Memory', 'Cpp', 'Runtime')) {
    if ($Linker -ne 'SharpProspero') { throw 'Memory diagnostic requires SharpProspero linker' }
    $probeFlags += '-DMKW_MEMORY_PROBE'
    $cppFlags = $target + @('-std=c++17', '-fno-exceptions', '-fno-rtti', "-I$PSScriptRoot/runtime")
    Run $Clang ($cppFlags + @('-Dmain=mkw_memory_test', '-D__prospero_klog=mkw_diagnostic_log', '-c', "$PSScriptRoot/diagnostics/guest_memory.cpp", '-o', "$out/memory_test.o"))
    Run $Clang ($cppFlags + @('-c', "$PSScriptRoot/runtime/guest_memory_ps5.cpp", '-o', "$out/memory_backend.o"))
    Run $Clang ($cppFlags + @('-DMKW_CPP_SMOKE_LIBRARY', '-c', "$PSScriptRoot/tests/cpp_runtime_smoke.cpp", '-o', "$out/cpp_smoke.o"))
    $extraObjects = @('--obj', "$out/memory_test.o", '--obj', "$out/memory_backend.o", '--obj', "$out/cpp_smoke.o")
    if ($ProbeKind -in @('Cpp', 'Runtime')) {
        if (!$LibcFile) { throw 'C++ ABI test requires the inspected console libc module' }
        $probeFlags += '-DMKW_CPP_ABI_PROBE'
        Run $Clang ($target + @('-std=c++17', '-fexceptions', '-frtti', '-funwind-tables', '-c', "$PSScriptRoot/tests/cpp_abi_native.cpp", '-o', "$out/cpp_abi.o"))
        # These imports are not all covered by SharpProspero's standard catalog.
        # Generate the extra stub using the actual module's library versions.
        @('__gxx_personality_v0', '_ZTIj', '_Znwm', '_ZdlPv', '_ZdlPvm') | Set-Content "$out/cpp-imports.txt" -Encoding utf8NoBOM
        $generator = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/bin/Release/net10.0/sharpprospero-bindgen.dll"
        Run $Dotnet @($generator, 'stub', '--module', $LibcFile, '--names', "$out/cpp-imports.txt", '--out', "$out/cpp-stub.a")
        $extraObjects += @('--obj', "$out/cpp_abi.o", '--stub', "$out/cpp-stub.a")
    }
    if ($ProbeKind -eq 'Runtime') {
        $probeFlags += '-DMKW_ENGINE_MEMORY_PROBE'
        $nativeCpp = @('--no-default-config', '--target=x86_64-unknown-freebsd9.3', "--sysroot=$root/.tools/ps5-sysroot", '-nostdinc++', '-isystem', "$root/artifacts/libcxx-build/include/c++/v1", '-std=c++17', '-O2', '-fPIC', '-fno-stack-protector', '-DMKW_PLATFORM_PS5=1', "-I$root/Wiicompiled/runtime/include", "-I$PSScriptRoot/runtime")
        $sources = @("$PSScriptRoot/diagnostics/engine_memory.cpp", "$PSScriptRoot/runtime/guest_flat_memory_ps5.cpp", "$root/Wiicompiled/runtime/src/memory.cpp", "$PSScriptRoot/runtime/c_locale_ps5.cpp", "$PSScriptRoot/runtime/cxx_platform_ps5.cpp", "$PSScriptRoot/tests/cxx_library_native.cpp", "$PSScriptRoot/tests/cpu_features_native.cpp", "$PSScriptRoot/tests/ppc_vectors_native.cpp")
        $sources+="$root/Wiicompiled/runtime/src/ppc_memory_helpers.cpp"
        $sources+="$PSScriptRoot/runtime/filesystem_ps5.cpp"
        $runtimeObjects = @()
        if($AuroraBootstrap){$nativeCpp+="-I$root/.tools/game-deps/SDL3-3.4.4/include"}
        if($NativeInput){$sources+="$PSScriptRoot/diagnostics/native_input.cpp"}
        if($SdlAudio){
            $sources+=@("$PSScriptRoot/diagnostics/audio_output.cpp","$root/Wiicompiled/runtime/src/audio_backend.cpp")
            $nativeCpp+=@("-I$root/.tools/game-deps/SDL3-3.4.4/include","-I$PSScriptRoot/sdl")
            $audio="$root/artifacts/sdl-audio/sdl/libSDL3.a"
            $extraObjects+=@('--lib',$audio);$runtimeObjects+=$audio
        }
        if($FileSystem){$sources+="$PSScriptRoot/diagnostics/absolute_files.cpp"}
        if($PlatformServices){
            $sources+=@("$PSScriptRoot/runtime/random_ps5.cpp","$PSScriptRoot/diagnostics/platform_services.cpp","$PSScriptRoot/diagnostics/native_dns.cpp","$PSScriptRoot/runtime/addrinfo_ps5.cpp","$PSScriptRoot/runtime/dns_native_ps5.cpp","$PSScriptRoot/diagnostics/addrinfo.cpp")
            $nativeCpp+=@("-I$root/Wiicompiled/runtime/third_party",'-DCRYPTOPP_DISABLE_ASM','-DCRYPTOPP_NO_GLOBAL_BYTE')
            $crypto="$root/artifacts/runtime-build/libmkw_ps5_cryptopp.a"
            $extraObjects+=@('--lib',$crypto);$runtimeObjects+=$crypto
        }
        if($SdlInput){
            $sources+="$PSScriptRoot/diagnostics/sdl_input.cpp"
            $nativeCpp+="-I$root/.tools/game-deps/SDL3-3.4.4/include"
            if($InspectSdlImports){$nativeCpp+='-DMKW_INSPECT_SDL_IMPORTS'}
            if($AuroraPad){
                $sources+=@("$PSScriptRoot/tests/aurora_pad.cpp","$root/Wiicompiled/aurora-main/lib/logging.cpp")
                $nativeCpp+=@('-DMKW_AURORA_PAD_PROBE','-DTARGET_PC=1','-DFMT_USE_FALLBACK_FILE=1','-DFMT_USE_FCNTL=0',
                    '-include','cstdlib',"-I$PSScriptRoot/input","-I$root/Wiicompiled/aurora-main/include",
                    "-I$root/Wiicompiled/aurora-main/lib","-I$root/.tools/desktop-check/abseil-cpp-20240722.0",
                    "-I$root/.tools/game-deps/fmt-11.1.4/include",'-std=c++20')
            }
        }
        for ($index = 0; $index -lt $sources.Count; ++$index) {
            $sourceFlags = @()
            if ($sources[$index] -like '*ppc_vectors_native.cpp') { $sourceFlags = @('-march=x86-64-v3', '-fno-fast-math', '-ffp-contract=off') }
            Run $Clang ($nativeCpp + $sourceFlags + @('-c', $sources[$index], '-o', "$out/engine_$index.o"))
            $extraObjects += @('--obj', "$out/engine_$index.o")
            $runtimeObjects += "$out/engine_$index.o"
        }
        if ($NativeInput) {
            $path="$root/artifacts/runtime-build/libmkw_ps5_native_input.a"
            $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
        }
        if ($SdlInput -or $AuroraBootstrap) {
            foreach($path in @("$root/artifacts/runtime-build/libmkw_ps5_virtual_pad.a", "$root/artifacts/sdl-input/sdl/libSDL3.a")) {
                $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
            }
            if($AuroraPad -or $AuroraBootstrap){
                $padArchives=@("$root/artifacts/runtime-build/libmkw_ps5_aurora_input.a","$root/artifacts/runtime-build/libmkw_ps5_fmt.a")
                $padArchives+="$root/artifacts/runtime-build/libmkw_ps5_native_input.a"
                $padArchives+=@((Get-ChildItem "$root/artifacts/runtime-build/input/aurora/abseil" -Recurse -File -Filter '*.a').FullName)
                foreach($path in $padArchives){$extraObjects+=@('--lib',$path);$runtimeObjects+=$path}
            }
        }
        # Every Runtime probe now exercises libc++ filesystem error handling.
        # Its shared native C++ tests therefore need the verified POSIX imports.
        if ($ProbeKind -eq 'Runtime') {
            & "$PSScriptRoot/New-PosixStub.ps1" -Output "$out/posix.o"
            $extraObjects+=@('--stub',"$out/posix.o",'--stub',"$out/posix.o.kernel.o")
        }
        if ($FramebufferSample) {
            $framebufferFlags = @()
            if($InspectDepthDefaults){$framebufferFlags+='-DMKW_INSPECT_DEPTH_DEFAULTS'}
            if($DepthTest){$framebufferFlags+='-DMKW_DEPTH_TEST'}
            if ($OwnedColorTargets) { $framebufferFlags += '-DMKW_OWNED_COLOR_TARGETS' }
            if ($GxCopyBinding) { $framebufferFlags += '-DMKW_GX_COPY_BINDING' }
            if ($GpuBlit) { $framebufferFlags += '-DMKW_GPU_BLIT' }
            if ($CopyShaderFile) { $framebufferFlags += '-DMKW_GX_FILTERED_COPY' }
            if ($CopyFormats) { $framebufferFlags += '-DMKW_GX_COPY_FORMATS' }
            if ($CopyReadback) { $framebufferFlags += '-DMKW_GX_COPY_READBACK' }
            if ($GxCopyExecute) {
                $framebufferFlags += @('-DMKW_GX_COPY_EXECUTE','-DTARGET_PC=1',
                    '-DFMT_USE_FALLBACK_FILE=1','-DFMT_USE_FCNTL=0','-include','cstdlib',
                    "-I$root/Wiicompiled/aurora-main/include","-I$root/Wiicompiled/aurora-main/lib",
                    "-I$root/.tools/game-deps/fmt-11.1.4/include")
            }
            Run $Clang ($nativeCpp + $framebufferFlags + @('-std=c++20',"-I$PSScriptRoot/gpu",'-c',"$PSScriptRoot/diagnostics/framebuffer_sample.cpp",'-o',"$out/framebuffer_sample.o"))
            $extraObjects += @('--obj',"$out/framebuffer_sample.o",'--lib',"$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a")
            $runtimeObjects += @("$out/framebuffer_sample.o","$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a")
            if($DepthTest){
                Run $Clang ($nativeCpp+@('-std=c++20',"-I$PSScriptRoot/gpu",'-c',"$PSScriptRoot/diagnostics/depth_sample.cpp",'-o',"$out/depth_sample.o"))
                $extraObjects+=@('--obj',"$out/depth_sample.o");$runtimeObjects+="$out/depth_sample.o"
            }
            if ($GxCopyBinding) {
                $copyFlags=@('-std=c++20','-DTARGET_PC=1','-DFMT_USE_FALLBACK_FILE=1','-DFMT_USE_FCNTL=0','-include','cstdlib',"-I$PSScriptRoot/gpu","-I$root/Wiicompiled/aurora-main/include","-I$root/Wiicompiled/aurora-main/lib","-I$root/.tools/game-deps/fmt-11.1.4/include")
                if($CopyReadback){$copyFlags+='-DMKW_GX_COPY_READBACK'}
                if($GxCopyExecute){$copyFlags+='-DMKW_GX_COPY_EXECUTE'}
                if($DepthTest){$copyFlags+='-DMKW_DEPTH_TEST'}
                Run $Clang ($nativeCpp+$copyFlags+@('-c',"$PSScriptRoot/diagnostics/copy_binding.cpp",'-o',"$out/copy_binding.o"))
                $extraObjects+=@('--obj',"$out/copy_binding.o");$runtimeObjects+="$out/copy_binding.o"
                foreach($archive in @('gx_material','gx_tev','gx_textures','gx_frontend','gx_registers','texture_decode','xxhash','fmt')) {
                    $path="$root/artifacts/runtime-build/libmkw_ps5_$archive.a"
                    $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
                }
                if($GxCopyExecute){$path="$root/artifacts/runtime-build/libmkw_ps5_gx_copy_commands.a";$extraObjects+=@('--lib',$path);$runtimeObjects+=$path}
            }
        }
        if ($WiiTextureDecode) {
            $probeFlags += '-DMKW_WII_TEXTURE_PROBE'
            $textureFlags = @('-std=c++20', '-DTARGET_PC=1', '-DFMT_USE_FALLBACK_FILE=1', '-DFMT_USE_FCNTL=0',
                "-I$root/Wiicompiled/aurora-main/include", "-I$root/Wiicompiled/aurora-main/lib", "-I$root/.tools/game-deps/fmt-11.1.4/include")
            Run $Clang ($nativeCpp + $textureFlags + @('-c', "$PSScriptRoot/tests/texture_decode.cpp", '-o', "$out/wii_texture.o"))
            $extraObjects += @('--obj', "$out/wii_texture.o")
            $runtimeObjects += "$out/wii_texture.o"
            foreach ($archive in @('texture_decode', 'fmt')) {
                $archivePath = "$root/artifacts/runtime-build/libmkw_ps5_$archive.a"
                $extraObjects += @('--lib', $archivePath)
                $runtimeObjects += $archivePath
            }
            if ($TextureUpload) {
                $probeFlags += '-DMKW_GENERAL_TEXTURE'
                Run $Clang ($nativeCpp + $textureFlags + @("-I$PSScriptRoot/gpu", '-c', "$PSScriptRoot/diagnostics/texture_upload.cpp", '-o', "$out/texture_upload.o"))
                $extraObjects += @('--obj', "$out/texture_upload.o", '--lib', "$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a")
                $runtimeObjects += @("$out/texture_upload.o", "$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a")
            }
        }
        $extraObjects += @('--lib', "$root/artifacts/libcxx-build/lib/libc++.a")
        if ($TevDirect) {
            $directFlags=@('-std=c++20','-march=x86-64-v3','-fno-fast-math','-ffp-contract=off','-DTARGET_PC=1','-include','cstdlib',
                '-DFMT_USE_FALLBACK_FILE=1','-DFMT_USE_FCNTL=0',"-I$PSScriptRoot/gpu",
                "-I$root/Wiicompiled/aurora-main/include","-I$root/Wiicompiled/aurora-main/lib",
                "-I$root/.tools/game-deps/fmt-11.1.4/include","-I$root/.tools/game-deps/xxHash-0.8.3")
            $directUnits=@(@("$PSScriptRoot/diagnostics/tev_direct.cpp",'tev_direct'),@("$root/Wiicompiled/aurora-main/lib/logging.cpp",'aurora_logging'))
            if ($GxGeometry) {
                $directFlags+='-DMKW_GEOMETRY_PROBE'
                if ($RawVertex) { $directFlags+='-DMKW_RAW_VERTEX' }
                if ($GxTransforms) { $directFlags+='-DMKW_GX_TRANSFORMS' }
                if ($CurrentMatrix) { $directFlags+='-DMKW_CURRENT_MATRIX' }
                if ($GxLighting) { $directFlags+='-DMKW_GX_LIGHTING' }
                if ($GxTexgen) { $directFlags+='-DMKW_GX_TEXGEN' }
                if ($GxVaryings) { $directFlags+='-DMKW_GX_VARYINGS' }
                if ($FragmentOutput) { $directFlags+='-DMKW_GX_FRAGMENT_OUTPUT' }
                if ($GxBlend) { $directFlags+='-DMKW_GX_BLEND';$directUnits+=,@("$PSScriptRoot/diagnostics/blend.cpp",'blend') }
                if ($GxViewport) { $directFlags+='-DMKW_GX_VIEWPORT';$directUnits+=,@("$PSScriptRoot/diagnostics/viewport.cpp",'viewport') }
                $directUnits+=,@("$PSScriptRoot/diagnostics/gx_geometry.cpp",'gx_geometry')
            }
            if($GxDraw){$directUnits+=,@("$PSScriptRoot/diagnostics/gx_draw_sample.cpp",'gx_draw_sample');$directUnits+=,@("$PSScriptRoot/tests/gx_memory_sources.cpp",'gx_memory_sources_test')}
            if($GxDisplayCopy){$directFlags+='-DMKW_GX_DISPLAY_COPY'}
            if($GxPresent){$directFlags+='-DMKW_GX_PRESENT'}
            if($GxLifecycle){$directFlags+='-DMKW_GX_LIFECYCLE'}
            if($AuroraBootstrap){$directFlags+='-DMKW_AURORA_BOOTSTRAP';$directUnits+=,@("$PSScriptRoot/diagnostics/aurora_bootstrap_sample.cpp",'aurora_bootstrap_sample')}
            if($GxLifecycle){$directUnits+=,@("$PSScriptRoot/diagnostics/gx_frame_sample.cpp",'gx_frame_sample')}
            if($OwnedShaders){$directUnits+=,@("$PSScriptRoot/diagnostics/gx_owned_shader_sample.cpp",'gx_owned_shader_sample')}
            foreach ($unit in $directUnits) {
                $obj="$out/$($unit[1]).o"
                Run $Clang ($nativeCpp+$directFlags+@('-c',$unit[0],'-o',$obj))
                $extraObjects+=@('--obj',$obj);$runtimeObjects+=$obj
            }
            foreach ($archive in @('gx_material','gx_tev','gx_textures','gx_frontend','gx_registers','agc_texture','texture_decode','xxhash','fmt')) {
                $path="$root/artifacts/runtime-build/libmkw_ps5_$archive.a"
                $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
            }
            if ($GxViewport -or $GxDraw) {
                $path="$root/artifacts/runtime-build/libmkw_ps5_gx_viewport.a"
                $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
            }
            if ($GxLifecycle) {
                $path="$root/artifacts/runtime-build/libmkw_ps5_gx_copy_commands.a"
                $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
            }
            if ($GxGeometry) {
                $path="$root/artifacts/runtime-build/libmkw_ps5_gx_geometry.a"
                $extraObjects+=@('--lib',$path);$runtimeObjects+=$path
            }
        }
        & "$PSScriptRoot/New-VerifiedLibcStub.ps1" -Module $LibcFile -Objects ($runtimeObjects + @("$root/artifacts/libcxx-build/lib/libc++.a")) -Output "$out/runtime-libc.a"
        $extraObjects += @('--stub', "$out/runtime-libc.a")
    }
}
# Preserve the actual archive inputs before linking. A later incremental build
# must not replace the only available copy of a console-tested dependency.
$linkInputArchives = @()
for ($i=0; $i -lt $extraObjects.Count-1; ++$i) {
    if ($extraObjects[$i] -ne '--lib') { continue }
    $source = $extraObjects[$i+1]
    New-Item -ItemType Directory -Force "$out/link-inputs" | Out-Null
    $snapshot = "$out/link-inputs/$i-$(Split-Path $source -Leaf)"
    Copy-Item -LiteralPath $source -Destination $snapshot
    $linkInputArchives += @{source=$source;snapshot=$snapshot;sha256=(Get-FileHash $snapshot).Hash}
    $extraObjects[$i+1] = $snapshot
}
Run $Clang ($target + $probeFlags + @('-c', '-Wall', "-I$out", ('-DMKW_PROBE_TITLE="' + $TitleId + '"'), "$PSScriptRoot/gpu_probe.c", '-o', "$out/probe.o"))
Run $Clang ($target + @('-c', "$sdk/examples/hello_notify/main.c", '-o', "$out/test_input.o"))
foreach ($test in @('nid', 'catalog', 'elf_object', 'linker')) {
    Run $Clang (@('-O2', '-o', "$out/test_$test.exe", "$sdk/linker/test_$test.c") + $libs)
    $testArgs = @()
    if ($test -eq 'elf_object') { $testArgs = @("$out/test_input.o") }
    if ($test -eq 'linker') { $testArgs = @("$out/crt1.o", "$out/test_input.o") }
    Run "$out/test_$test.exe" $testArgs
}
# link_real also embeds argv[1] as the module's original filename. Keep the
# host's absolute Windows path out of the console module's identity.
$signProject = "$root/SharpProspero/tools/SharpProspero.Bindings.Generator/SharpProspero.Bindings.Generator.csproj"
if ($Linker -eq 'SharpProspero') {
    Run $Dotnet @('build', $signProject, '-c', 'Release', '--no-restore', '-v', 'quiet')
    Run $Dotnet (@('run', '--project', $signProject, '-c', 'Release', '--no-build', '--',
        'link', '--obj', "$out/probe.o", '--self-contained', '--kind', 'eboot', '--out', "$out/probe.elf") + $extraObjects) | Tee-Object -FilePath "$out/link.log"
    if ($ProbeKind -eq 'Runtime') {
        & "$PSScriptRoot/Test-LibcImports.ps1" -Module $LibcFile -LinkLog "$out/link.log" -Output "$out/linked-libc-imports.json"
    }
} else {
    Push-Location $out
    try { Run "$out/link_real.exe" @('probe.elf', 'crt1.o', 'probe.o') }
    finally { Pop-Location }
}
Run $Dotnet @('run', '--project', $signProject, '-c', 'Release', '--no-build', '--', 'self', '--sign', '--in', "$out/probe.elf", '--out', "$package/eboot.bin")
$param = Get-Content "$root/SharpProspero/samples/prospero-3d/sce_sys/param.json" -Raw | ConvertFrom-Json
$param.titleId = $TitleId
$param.conceptId = $TitleId.Substring(4)
$param.contentId = "IV0000-${TitleId}_00-WIICOMPILEDAGC01"
$param.localizedParameters.'en-US'.titleName = "WiiCompiled PS5 - $ProbeKind probe $TitleId"
$param | ConvertTo-Json -Depth 8 | Set-Content "$package/sce_sys/param.json" -Encoding utf8NoBOM
Run $Dotnet @('run', '--project', $signProject, '-c', 'Release', '--no-build', '--', 'param', '--folder', $package)
Copy-Item -LiteralPath "$root/SharpProspero/samples/prospero-3d/sce_sys/icon0.png" -Destination "$package/sce_sys/icon0.png"
$missing = @('compatible libc.prx')
if ($LibcFile) {
    $LibcFile = (Resolve-Path -LiteralPath $LibcFile).Path
    $magic = [IO.File]::ReadAllBytes($LibcFile)
    if ($magic.Length -lt 64) { throw 'libc.prx is empty or truncated' }
    $signature = [BitConverter]::ToString($magic, 0, 4)
    if ($signature -eq '7F-45-4C-46') {
        Run $Dotnet @('run', '--project', $signProject, '-c', 'Release', '--no-build', '--', 'self', '--sign', '--in', $LibcFile, '--out', "$package/sce_module/libc.prx")
    } elseif ($signature -in @('54-14-F5-EE', '4F-15-3D-1D')) {
        Copy-Item -LiteralPath $LibcFile -Destination "$package/sce_module/libc.prx"
    } else { throw "Unrecognized libc.prx container: $signature" }
    Run $Dotnet @('run', '--project', $signProject, '-c', 'Release', '--no-build', '--', 'self', '--inspect', '--file', "$package/sce_module/libc.prx")
    $missing = @()
}
if ($missing.Count -eq 0) {
    # The package must declare at least the SDK level required by its modules.
    Run $Dotnet @('run', '--project', $signProject, '-c', 'Release', '--no-build', '--', 'sysver', '--folder', $package, '--policy', 'match', '--apply')
}
$files = @{}
if($AuroraBootstrap){
    New-Item -ItemType Directory -Force "$package/shaders" | Out-Null
    $shaderFiles=@{'gx_vertex.sb'=$VertexShaderFile;'gx_pixel.sb'=$PixelShaderFile;'copy_vertex.sb'="$sdk/shaders/third_party/sharpprospero/mesh_vs.sb";'copy_pixel.sb'=$CopyShaderFile}
    foreach($name in $shaderFiles.Keys){Copy-Item -LiteralPath $shaderFiles[$name] -Destination "$package/shaders/$name";$files["shaders/$name"]=(Get-FileHash "$package/shaders/$name").Hash}
}
foreach ($path in @('eboot.bin', 'sce_sys/param.json', 'sce_sys/icon0.png', 'sce_module/libc.prx')) {
    if (Test-Path "$package/$path") { $files[$path] = (Get-FileHash "$package/$path").Hash }
}
@{
    kind = "$ProbeKind hardware probe; not Mario Kart Wii"
    linker = $Linker
    target = 'PS5 Pro'; firmware = '9.40'; hardwareVerified = $false
    completePackage = ($missing.Count -eq 0); missing = $missing
    titleId = $TitleId; files = $files
    ebootSha256 = (Get-FileHash "$package/eboot.bin").Hash
    textureDraw = $TextureDraw.IsPresent
    framebufferSample = $FramebufferSample.IsPresent
    ownedColorTargets = $OwnedColorTargets.IsPresent
    gxCopyBinding = $GxCopyBinding.IsPresent
    inspectFence = $InspectFence.IsPresent
    gpuFence = $GpuFence.IsPresent
    gpuBlit = $GpuBlit.IsPresent
    copyShaderSha256 = $(if($CopyShaderFile){(Get-FileHash -LiteralPath $CopyShaderFile).Hash}else{$null})
    copyFormats = $CopyFormats.IsPresent
    copyReadback = $CopyReadback.IsPresent
    gxCopyExecute = $GxCopyExecute.IsPresent
    inspectDepthDefaults = $InspectDepthDefaults.IsPresent
    depthTest = $DepthTest.IsPresent
    gxDraw = $GxDraw.IsPresent
    gxDisplayCopy = $GxDisplayCopy.IsPresent
    gxPresent = $GxPresent.IsPresent
    gxLifecycle = $GxLifecycle.IsPresent
    ownedShaders = $OwnedShaders.IsPresent
    auroraBootstrap = $AuroraBootstrap.IsPresent
    nativeInput = $NativeInput.IsPresent
    sdlInput = $SdlInput.IsPresent
    sdlAudio = $SdlAudio.IsPresent
    inspectSdlImports = $InspectSdlImports.IsPresent
    fileSystem = $FileSystem.IsPresent
    platformServices = $PlatformServices.IsPresent
    auroraPad = $AuroraPad.IsPresent
    linkInputArchives = @($linkInputArchives)
    wiiTextureDecode = $WiiTextureDecode.IsPresent
    textureUpload = $TextureUpload.IsPresent
    tevArithmetic = $TevArithmetic.IsPresent
    tevProgram = $TevProgram.IsPresent
    tevDirect = $TevDirect.IsPresent
    gxGeometry = $GxGeometry.IsPresent
    rawVertex = $RawVertex.IsPresent
    gxTransforms = $GxTransforms.IsPresent
    currentMatrix = $CurrentMatrix.IsPresent
    gxLighting = $GxLighting.IsPresent
    gxTexgen = $GxTexgen.IsPresent
    gxVaryings = $GxVaryings.IsPresent
    fragmentOutput = $FragmentOutput.IsPresent
    gxBlend = $GxBlend.IsPresent
    gxViewport = $GxViewport.IsPresent
    fp32Output = $Fp32Output.IsPresent
    unorm16Output = $Unorm16Output.IsPresent
    inspectLinkage = $InspectLinkage.IsPresent
    pixelShaderSha256 = $(if ($PixelShaderFile) { (Get-FileHash -LiteralPath $PixelShaderFile).Hash } else { $null })
    vertexShaderSha256 = $(if ($VertexShaderFile) { (Get-FileHash -LiteralPath $VertexShaderFile).Hash } else { $null })
    sources = @{
        ps5link = (& git -C $sdk rev-parse HEAD)
        sharpProspero = (& git -C "$root/SharpProspero" rev-parse HEAD)
        sharpProsperoPatch = (Get-FileHash "$PSScriptRoot/patches/sharpprospero-comdat-frames.patch").Hash
        probe = (Get-FileHash "$PSScriptRoot/gpu_probe.c").Hash
        textureProbe = $(if ($TextureDraw) { (Get-FileHash "$PSScriptRoot/diagnostics/agc_texture.c").Hash } else { $null })
        linkageDiagnostic = $(if ($InspectLinkage) { (Get-FileHash "$PSScriptRoot/diagnostics/agc_linkage.c").Hash } else { $null })
        blendDiagnostic = $(if ($GxBlend) { (Get-FileHash "$PSScriptRoot/diagnostics/blend.cpp").Hash } else { $null })
        viewportDiagnostic = $(if ($GxViewport) { (Get-FileHash "$PSScriptRoot/diagnostics/viewport.cpp").Hash } else { $null })
        gxViewportArchive = $(if ($GxViewport) { (Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_gx_viewport.a").Hash } else { $null })
        framebufferSampleDiagnostic = $(if ($FramebufferSample) { (Get-FileHash "$PSScriptRoot/diagnostics/framebuffer_sample.cpp").Hash } else { $null })
        copyBindingDiagnostic = $(if ($GxCopyBinding) { (Get-FileHash "$PSScriptRoot/diagnostics/copy_binding.cpp").Hash } else { $null })
        copyBindingArchives = $(if ($GxCopyBinding) { $names=@('gx_material','gx_tev','gx_textures','gx_frontend','gx_registers','agc_texture','texture_decode','xxhash','fmt');if($GxCopyExecute){$names+='gx_copy_commands'};$names | ForEach-Object { @{ name=$_;sha256=(Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_$_.a").Hash } } } else { $null })
        framebufferTextureArchive = $(if ($FramebufferSample) { (Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a").Hash } else { $null })
        fixtureGenerator = $(if ($TextureDraw) { (Get-FileHash "$PSScriptRoot/tools/agc_fixture/Program.cs").Hash } else { $null })
        agcTextureArchive = $(if ($TextureUpload) { (Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_agc_texture.a").Hash } else { $null })
        textureUploadDiagnostic = $(if ($TextureUpload) { (Get-FileHash "$PSScriptRoot/diagnostics/texture_upload.cpp").Hash } else { $null })
        tevCases = $(if ($TevArithmetic) { (Get-FileHash "$tevDir/cases.json").Hash } else { $null })
        tevArithmetic = $(if ($TevArithmetic) { (Get-FileHash "$PSScriptRoot/gpu/tev_integer.h").Hash } else { $null })
        tevPrograms = $(if ($TevProgram) { (Get-FileHash "$tevDir/programs.bin").Hash } else { $null })
        tevStage = $(if ($TevProgram) { (Get-FileHash "$PSScriptRoot/gpu/tev_stage.h").Hash } else { $null })
        tevDirectDiagnostic = $(if ($TevDirect) { (Get-FileHash "$PSScriptRoot/diagnostics/tev_direct.cpp").Hash } else { $null })
        gxGeometryDiagnostic = $(if ($GxGeometry) { (Get-FileHash "$PSScriptRoot/diagnostics/gx_geometry.cpp").Hash } else { $null })
        gxGeometryArchive = $(if ($GxGeometry) { (Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_gx_geometry.a").Hash } else { $null })
        tevDirectShader = $(if ($TevDirect) { (Get-FileHash "$PSScriptRoot/gpu/shaders/tev_direct_probe.c").Hash } else { $null })
        tevDirectArchives = $(if ($TevDirect) {
            @('gx_material','gx_tev','gx_textures','gx_frontend','gx_registers','agc_texture','texture_decode','xxhash','fmt') | ForEach-Object {
                @{name=$_;sha256=(Get-FileHash "$root/artifacts/runtime-build/libmkw_ps5_$_.a").Hash}
            }
        } else { $null })
    }
} | ConvertTo-Json -Depth 5 | Set-Content "$out/build.json" -Encoding utf8NoBOM
Write-Host "Built and signed: $package"
if ($missing.Count) { Write-Host 'Package incomplete: pass -LibcFile with a compatible module before installation.' }
else { Write-Host 'Package assembled. Firmware compatibility still requires a console test.' }
