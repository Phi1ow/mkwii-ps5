[CmdletBinding()]
param(
    # Without -Apply the script only lists what it would move.
    [switch]$Apply,
    # Most recent folders kept in each category, in addition to referenced ones.
    [ValidateRange(1, 20)][int]$KeepRecent = 2
)
# Lightens artifacts/ by moving regenerable build outputs to the Windows
# Recycle Bin (recoverable until the bin is emptied). Categories: executable
# builds (eboot + ELF + object snapshots, ~490 MB each), update backups of
# previous eboots, game packages and GPU probe payloads. Always kept: every
# folder named by a ps5 script or tool, the build of the installed eboot (per
# the latest artifacts/game-updates/*/result.json) and the most recent folders.
# Build trees (runtime-build, game-build, libcxx-build, sdl-*), run logs
# (game-native), screenshots and documentation are never touched.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $root 'artifacts'
$categories = @('game-executable', 'game-updates', 'game-package', 'gpu-probe')

# Folders referenced by scripts and tools.
$referenced = @{}
Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File -Include *.ps1, *.py |
    Where-Object { $_.FullName -ne $PSCommandPath } |
    ForEach-Object {
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $_.FullName -Raw), 'artifacts/([A-Za-z0-9_-]+)/([0-9A-Za-z_-]+)')) {
            $referenced["$($match.Groups[1].Value)/$($match.Groups[2].Value)"] = $true
        }
    }

# Build folder of the installed eboot.
$installed = $null
$latestUpdate = Get-ChildItem -LiteralPath (Join-Path $artifacts 'game-updates') -Directory -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1
if ($latestUpdate -and (Test-Path (Join-Path $latestUpdate.FullName 'result.json'))) {
    $sha = (Get-Content (Join-Path $latestUpdate.FullName 'result.json') -Raw | ConvertFrom-Json).sha256
    Get-ChildItem -LiteralPath (Join-Path $artifacts 'game-executable') -Directory | ForEach-Object {
        $build = Join-Path $_.FullName 'build.json'
        if ((Test-Path $build) -and ((Get-Content $build -Raw | ConvertFrom-Json).ebootSha256 -ieq $sha)) { $installed = $_.Name }
    }
}

$moves = @()
foreach ($category in $categories) {
    $folders = @(Get-ChildItem -LiteralPath (Join-Path $artifacts $category) -Directory -ErrorAction SilentlyContinue | Sort-Object LastWriteTime)
    $recent = @($folders | Select-Object -Last $KeepRecent | ForEach-Object Name)
    foreach ($folder in $folders) {
        $key = "$category/$($folder.Name)"
        if ($referenced.ContainsKey($key) -or $recent -contains $folder.Name -or ($category -eq 'game-executable' -and $folder.Name -eq $installed)) { continue }
        $bytes = (Get-ChildItem -LiteralPath $folder.FullName -Recurse -File | Measure-Object Length -Sum).Sum
        $moves += [pscustomobject]@{ Path = $folder.FullName; Key = $key; Bytes = [int64]$bytes }
    }
}

$total = ($moves | Measure-Object Bytes -Sum).Sum
Write-Host ("{0} folders, {1:N1} GiB regenerable (installed build: {2})" -f $moves.Count, ($total / 1GB), $(if ($installed) { $installed } else { 'not found' }))
if (!$Apply) {
    $moves | Sort-Object Bytes -Descending | Select-Object -First 15 | ForEach-Object { Write-Host ("  {0,8:N0} MiB  {1}" -f ($_.Bytes / 1MB), $_.Key) }
    Write-Host 'Dry run. Re-run with -Apply to move them to the Recycle Bin.'
    return
}
Add-Type -AssemblyName Microsoft.VisualBasic
foreach ($move in $moves) {
    [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($move.Path,
        [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs,
        [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
}
Write-Host ("Moved {0} folders ({1:N1} GiB) to the Recycle Bin." -f $moves.Count, ($total / 1GB))
