# The long part of the setup, run in the background by mw4vr_setup.ps1: exports the cockpits of the player's own
# MechWarrior 5: Mercenaries with UE Viewer and converts them for MW4VR. Everything it says goes into -Log.
# Exit code: 0 done, 1 the Python packages, 2 the export, 3 the conversion.
param([string]$Tools, [string]$Paks, [string]$Umodel, [string]$Game, [string]$Python, [string]$PythonArg = '', [string]$Log,
      [switch]$Pip, [switch]$DeleteExport, [string]$Mods = '')
$ErrorActionPreference = 'Continue'
function Say([string]$t) { Add-Content -Path $Log -Value $t -Encoding UTF8 }
function Run([string]$exe, [string[]]$a) {
    & $exe @a 2>&1 | ForEach-Object { Say "$_" }
    return $LASTEXITCODE
}
$py = @(); if ($PythonArg) { $py += $PythonArg }
$export = Join-Path $Game 'mw4vr_export'
$out = Join-Path $Game 'mw4vr_cockpits'

if ($Pip) {
    # (within the versions the converter was run with: not whatever is newest on the day)
    Say 'Installing the Python packages numpy (1.24 to 2.x) and Pillow (9 to 13) ...'
    if ((Run $Python ($py + @('-m', 'pip', 'install', '--user', 'numpy>=1.24,<3', 'pillow>=9,<14'))) -ne 0) { Say 'pip failed.'; exit 1 }
}
Say 'Exporting the cockpits from MechWarrior 5 (about 2 GB) ...'
$code = Run (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') (@('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $Tools 'export_cockpits.ps1'), '-Paks', $Paks, '-Out', $export, '-Umodel', $Umodel) + $(if ($Mods) { @('-Mods', $Mods) } else { @() }))
if ($code -ne 0 -or -not (Test-Path (Join-Path $export 'cockpits.txt'))) { Say 'The export failed.'; exit 2 }
Say 'Converting the cockpits (about 4 GB) ...'
if ((Run $Python ($py + @((Join-Path $Tools 'convert_all.py'), $export, $out))) -ne 0) { Say 'The conversion failed.'; exit 3 }
$n = @(Get-ChildItem $out -Filter *.mwc -ErrorAction SilentlyContinue).Count
Say "$n cockpits are in $out."
if ($n -eq 0) { exit 3 }
if ($DeleteExport) {
    Remove-Item $export -Recurse -Force -ErrorAction SilentlyContinue
    Say 'The exported files are deleted.'
}
exit 0
