# Exports every mech cockpit of your own MechWarrior 5: Mercenaries with UE Viewer (umodel) as glTF + PNG.
#   powershell -File tools\export_cockpits.ps1 -Paks <...\MW5Mercs\Content\Paks> [-Out mw5export] [-Only Atlas,Catapult]
#       [-Mods <...\MW5Mercs\Mods>]     also the cockpits the mods installed there bring (into <Out>\mods)
# The export is Piranha Games' work (and the mods' authors'): for your own use, never to be passed on.
param(
    [Parameter(Mandatory = $true)][string]$Paks,
    [string]$Out = '',
    [string[]]$Only = @(),
    [string]$Umodel = '',
    [string]$Mods = ''
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Out) { $Out = Join-Path $here '..\mw5export' }
if (-not $Umodel) { $Umodel = Join-Path $here 'umodel\umodel_64.exe' }
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Umodel)) { throw "umodel not found: $Umodel (https://www.gildor.org/en/projects/umodel)" }
$Out = [IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force $Out | Out-Null
$IsMesh = '^\s+\d+\s+[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+SkeletalMesh\s'

# every package under $path, one a line, into a file (the list has millions of lines: too many to pass through
# PowerShell one by one) -> the file
function Get-PackageList([string]$path, [string]$file) {
    $p = Start-Process -FilePath $Umodel -ArgumentList "`"-path=$path`"", '-game=ue4.26', '-list', '*' -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $file -RedirectStandardError "$file.err"
    Remove-Item "$file.err" -ErrorAction SilentlyContinue
    return $file
}
# does the package hold a skeletal mesh? (-list takes one package)
function Test-Mesh([string]$path, [string]$pkg) {
    $objs = & $Umodel "-path=$path" '-game=ue4.26' '-list' $pkg 2>&1 | ForEach-Object { "$_" }
    return [bool]($objs -match $IsMesh)
}
function Export-Packages([string]$path, $pkgs, [string]$to) {
    $i = 0
    while ($i -lt @($pkgs).Count) {
        $part = @($pkgs)[$i..([Math]::Min($i + 19, @($pkgs).Count - 1))]
        # every argument quoted: Windows PowerShell would split "-game=ue4.26" at the dot
        $args_ = @("-path=$path", '-game=ue4.26', '-export', '-gltf', '-png', "-out=$to") + ($part | ForEach-Object { "-pkg=$($_.Pkg)" })
        # (UE Viewer cannot write a cockpit's animations as glTF from the command line and says so as an "ERROR" for
        # each; the meshes and pictures - all that is needed - are exported all the same, so that line is left out)
        & $Umodel @args_ 2>&1 | Select-String -Pattern '^Exported|ERROR|WARNING: .*failed' |
            Where-Object { $_.Line -notmatch 'glTF animation could be exported from mesh viewer only' } | ForEach-Object { $_.Line }
        $i += 20
    }
}

$list = & $Umodel "-path=$Paks" '-game=ue4.26' '-list' '*_Cockpit_SKM*' 2>&1
$pkgs = $list | ForEach-Object { if ($_ -match '(/Game/\S*/Model/Cockpit/(\w+)_Cockpit_SKM)\.uasset') { [pscustomobject]@{ Pkg = $Matches[1]; Mech = $Matches[2] } } } |
    Sort-Object Pkg -Unique
# A cockpit whose mesh has another name (the Arctic Wolf's is "acw"): every cockpit folder without a *_Cockpit_SKM
# is looked into, and the package in it that holds a skeletal mesh is the cockpit.
$tmp = Get-PackageList $Paks (Join-Path $Out 'packages.txt')
$inDirs = @(Select-String -Path $tmp -Pattern '^(/Game/\S*/Mechs/(?:CLANS/)?(\w+)/Model/Cockpit)/([^/\s]+)\.uasset\s*$' | ForEach-Object {
        [pscustomobject]@{ Dir = $_.Matches[0].Groups[1].Value; Mech = $_.Matches[0].Groups[2].Value; Pkg = $_.Matches[0].Groups[1].Value + '/' + $_.Matches[0].Groups[3].Value } })
Remove-Item $tmp -ErrorAction SilentlyContinue
$known = @{}
foreach ($p in $pkgs) { $known[($p.Pkg -replace '/[^/]+$', '').ToLower()] = 1 }
foreach ($grp in ($inDirs | Where-Object { -not $known[$_.Dir.ToLower()] } | Group-Object Dir)) {
    foreach ($name in @($grp.Group | ForEach-Object { $_.Pkg } | Sort-Object -Unique)) {
        if (Test-Mesh $Paks $name) {
            $pkgs = @($pkgs) + [pscustomobject]@{ Pkg = $name; Mech = $grp.Group[0].Mech }
            "a cockpit under another name: $name"
            break # (one a folder)
        }
    }
}
$pkgs = @($pkgs | Sort-Object Pkg -Unique)
if ($Only.Count) { $pkgs = @($pkgs | Where-Object { $Only -contains $_.Mech }) }
"{0} cockpits" -f @($pkgs).Count
Export-Packages $Paks $pkgs $Out
$rows = @($pkgs | ForEach-Object { "{0}`t{1}" -f $_.Mech, (Join-Path $Out (($_.Pkg -replace '^/', '') -replace '/', '\')) })

# The mods' cockpits: skeletal meshes in a folder named Cockpit inside a mod (/MW5Mercs/Plugins/<mod>/Content/...).
# They go into <Out>\mods, apart from the game's own: a mod may replace files of the game, and those must not
# get into the game's cockpits. A row of cockpits.txt for one of them names that folder as its third part.
# The name: the mech's folder; "<mech>_<mod>" if the game or another mod has a cockpit of that name already.
if ($Mods) {
    $root = Split-Path -Parent ([IO.Path]::GetFullPath($Mods).TrimEnd('\'))
    if (-not (Test-Path $Mods)) { "no mods folder: $Mods" }
    elseif (-not ([IO.Path]::GetFullPath($Paks).ToLower().StartsWith($root.ToLower()))) { "the mods folder is not beside the game's Content folder: mods left out" }
    else {
        $modOut = Join-Path $Out 'mods'
        New-Item -ItemType Directory -Force $modOut | Out-Null
        $tmp = Get-PackageList $root (Join-Path $Out 'packages.txt')
        $all = @(Select-String -Path $tmp -Pattern '^(/MW5Mercs/Plugins/([^/]+)/Content/(\S*))/([^/\s]+)\.uasset\s*$' | ForEach-Object {
                $g = $_.Matches[0].Groups
                [pscustomobject]@{ Mod = $g[2].Value; Dir = $g[1].Value; Rel = $g[3].Value; Name = $g[4].Value; Pkg = $g[1].Value + '/' + $g[4].Value } } |
            Where-Object { $_.Rel -match '(?i)(^|/)cockpit(/|$)' })
        Remove-Item $tmp -ErrorAction SilentlyContinue
        $taken = @{}
        foreach ($p in $pkgs) { $taken[$p.Mech.ToLower()] = 1 }
        $modPkgs = @()
        foreach ($grp in ($all | Group-Object Dir | Sort-Object Name)) {
            # the mech: the folder above Cockpit (and above Model, Body)
            $parts = @($grp.Group[0].Rel -split '/')
            $k = 0
            for ($j = 0; $j -lt $parts.Count; $j++) { if ($parts[$j] -ieq 'cockpit') { $k = $j; break } }
            do { $k-- } while ($k -ge 0 -and @('model', 'body') -contains $parts[$k].ToLower())
            if ($k -lt 0) { continue }
            $mech = $parts[$k]
            # the meshes: by their name first (..._SKM, not the glass, the physics, the skeleton), else whatever is there
            $cand = @($grp.Group | Where-Object { $_.Name -match '(?i)skm' -and $_.Name -notmatch '(?i)glass|physics|skeleton' } | Sort-Object Name)
            if (-not $cand.Count) { $cand = @($grp.Group | Where-Object { $_.Name -notmatch '(?i)glass|physics|skeleton|_(nrm|clr|ems|orm|msk|mti|mtl)\d*$' } | Sort-Object Name) }
            $n = 0
            foreach ($c in $cand) {
                if (-not (Test-Mesh $root $c.Pkg)) { continue }
                $n++
                $name = if ($n -gt 1) { "${mech}_$n" } else { $mech }
                if ($taken[$name.ToLower()]) { $name = "${name}_$($c.Mod)" }
                if ($taken[$name.ToLower()]) { continue }
                $taken[$name.ToLower()] = 1
                $modPkgs += [pscustomobject]@{ Pkg = $c.Pkg; Mech = $name }
            }
        }
        if ($Only.Count) { $modPkgs = @($modPkgs | Where-Object { $Only -contains $_.Mech }) }
        "{0} cockpits of mods" -f @($modPkgs).Count
        Export-Packages $root $modPkgs $modOut
        $rows += @($modPkgs | ForEach-Object { "{0}`t{1}`t{2}" -f $_.Mech, (Join-Path $modOut (($_.Pkg -replace '^/', '') -replace '/', '\')), $modOut })
    }
}
$rows | Set-Content (Join-Path $Out 'cockpits.txt') -Encoding utf8
"list written: $(Join-Path $Out 'cockpits.txt')"
