# Exports every mech cockpit of your own MechWarrior 5: Mercenaries with UE Viewer (umodel) as glTF + PNG.
#   powershell -File tools\export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks> [-Out mw5export] [-Only Atlas,Catapult]
# The export is Piranha Games' work: for your own use, never to be passed on.
param(
    [Parameter(Mandatory = $true)][string]$Paks,
    [string]$Out = '',
    [string[]]$Only = @(),
    [string]$Umodel = ''
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Out) { $Out = Join-Path $here '..\mw5export' }
if (-not $Umodel) { $Umodel = Join-Path $here 'umodel\umodel_64.exe' }
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Umodel)) { throw "umodel not found: $Umodel (https://www.gildor.org/en/projects/umodel)" }
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null

# every argument quoted: Windows PowerShell would split "-game=ue4.26" at the dot
$list = & $Umodel "-path=$Paks" '-game=ue4.26' '-list' '*_Cockpit_SKM*' 2>&1
$pkgs = $list | ForEach-Object { if ($_ -match '(/Game/\S*/Model/Cockpit/(\w+)_Cockpit_SKM)\.uasset') { [pscustomobject]@{ Pkg = $Matches[1]; Mech = $Matches[2] } } } |
    Sort-Object Pkg -Unique
if ($Only.Count) { $pkgs = $pkgs | Where-Object { $Only -contains $_.Mech } }
"{0} cockpits" -f @($pkgs).Count

$i = 0
while ($i -lt @($pkgs).Count) {
    $part = @($pkgs)[$i..([Math]::Min($i + 19, @($pkgs).Count - 1))]
    $args_ = @("-path=$Paks", '-game=ue4.26', '-export', '-gltf', '-png', "-out=$Out") + ($part | ForEach-Object { "-pkg=$($_.Pkg)" })
    # (UE Viewer cannot write a cockpit's animations as glTF from the command line and says so as an "ERROR" for
    # each; the meshes and pictures - all that is needed - are exported all the same, so that line is left out)
    & $Umodel @args_ 2>&1 | Select-String -Pattern '^Exported|ERROR|WARNING: .*failed' |
        Where-Object { $_.Line -notmatch 'glTF animation could be exported from mesh viewer only' } | ForEach-Object { $_.Line }
    $i += 20
}
$pkgs | ForEach-Object { "{0}`t{1}" -f $_.Mech, (Join-Path $Out (($_.Pkg -replace '^/', '') -replace '/', '\')) } | Set-Content (Join-Path $Out 'cockpits.txt') -Encoding utf8
"list written: $(Join-Path $Out 'cockpits.txt')"
