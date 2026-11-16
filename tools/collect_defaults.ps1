# Takes the values tuned in the game's folder into the repository as the defaults a release ships:
#   powershell -File tools\collect_defaults.ps1 -Game <the game's folder>
# defaults\mw4vr_mechs.ini          what is remembered for each mech (seat, sizes, the HUD's places, its cockpit)
# defaults\mw4vr_cockpits\*.ini     each MW5 cockpit's settings (eye, size, monitors, the parts on its glass)
# Only numbers and names of ours: no cockpit meshes, nothing of MechWarrior 5.
param([Parameter(Mandatory)][string]$Game)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dst = Join-Path $root 'defaults'
New-Item -ItemType Directory (Join-Path $dst 'mw4vr_cockpits') -Force | Out-Null
Copy-Item (Join-Path $Game 'mw4vr_mechs.ini') $dst -Force
$n = 0
Get-ChildItem (Join-Path $Game 'mw4vr_cockpits') -Filter *.ini | Where-Object { $_.BaseName } | ForEach-Object {
    Copy-Item $_.FullName (Join-Path $dst 'mw4vr_cockpits') -Force
    $n++
}
"mw4vr_mechs.ini and $n cockpit files -> $dst"
