param([string]$Rom, [string]$BuildDir='build_release', [string]$EngineRoot, [string]$RecompUi, [string]$RedistDir, [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) { $EngineRoot=Join-Path $root 'nesrecomp' }
if (-not $Rom) {
    $images=@(Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Extension -ieq '.nes' })
    if ($images.Count -eq 1) { $Rom=$images[0].FullName }
}
$notes = @'
Optional experimental Seamless mod: open Mods and enable Seamless, the first
mod in the list and marked Experimental. Connected rooms form a continuous
world with moving neighboring actors and adaptive Fit to screen.
Resize the window freely. Mouse wheel changes pixel zoom; F4 shows the map.
Start pauses, Select opens native inventory, B closes it.
Doors and scripted region changes retain their native transitions.
Seamless passed a preliminary owner playtest; cross-room combat/projectiles
and broader progression coverage remain incomplete. It is disabled by default
and local-play only.
'@
$buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Rom $Rom -BuildDir $buildPath -EngineRoot $EngineRoot -RecompUi $RecompUi
    if ($LASTEXITCODE) { throw 'Cycle build failed.' }
}
& (Join-Path $PSScriptRoot 'stage_windows_runtime.ps1') -BuildDir $buildPath -RedistDir $RedistDir
& (Join-Path $EngineRoot 'tools/package_cycle_windows.ps1') -ProjectRoot $root -Target 'FaxanaduRecomp' -Title 'Faxanadu' -BuildDir $buildPath -Rom $Rom -EngineRoot $EngineRoot -RecompUi $RecompUi -SkipBuild -GameNotes $notes
