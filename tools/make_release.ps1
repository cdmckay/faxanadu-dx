param([string]$Rom, [string]$BuildDir='build_release', [string]$EngineRoot, [string]$RecompUi, [string]$RedistDir, [switch]$SkipBuild)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (-not $EngineRoot) { $EngineRoot=Join-Path $root 'nesrecomp' }
if (-not $Rom) {
    $images=@(Get-ChildItem -LiteralPath $root -File | Where-Object { $_.Extension -ieq '.nes' })
    if ($images.Count -eq 1) { $Rom=$images[0].FullName }
}
$notes = @'
Faxanadu Remastered plays in Seamless: connected rooms form one continuous
world, with no stop to scroll between them and the next room's actors moving,
in a fixed 424x240 view scaled to the window. F4 shows the region. Doors,
region changes and each boss's room (while its boss lives) keep the
original's own transitions. Switch Faxanadu Remastered off in Mods to play
the original game in its own view. Local play only.
'@
$buildPath = if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Rom $Rom -BuildDir $buildPath -EngineRoot $EngineRoot -RecompUi $RecompUi
    if ($LASTEXITCODE) { throw 'Cycle build failed.' }
}
& (Join-Path $PSScriptRoot 'stage_windows_runtime.ps1') -BuildDir $buildPath -RedistDir $RedistDir
& (Join-Path $EngineRoot 'tools/package_cycle_windows.ps1') -ProjectRoot $root -Target 'FaxanaduRecomp' -Title 'Faxanadu' -BuildDir $buildPath -Rom $Rom -EngineRoot $EngineRoot -RecompUi $RecompUi -SkipBuild -GameNotes $notes
