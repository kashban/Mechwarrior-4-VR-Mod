# MW4VR setup: a window that puts the mod into the game's folder, sets MW4Engine's settings, installs the trees
# and - for owners of MechWarrior 5: Mercenaries - exports and converts its cockpits.
# Started by MW4VR_Setup.cmd from the unpacked package (beside the folders game, cockpit_tools, trees).
#   -NoGui: only the functions (for tests):  . .\mw4vr_setup.ps1 -NoGui
param([switch]$NoGui)
$ErrorActionPreference = 'Stop'
$script:Pkg = $PSScriptRoot
$script:LogFile = Join-Path $env:TEMP 'mw4vr_setup.log'
$script:Say = { param($t) Write-Host $t }
$KnownHookStamp = 0x6aef4435   # MW4Engine DX11 build 20261107_ea16ef2 (the PE header's time stamp)
$HookKeys = [ordered]@{ antihack = '0'; fov = '130'; vsync = '0'; taa = '0'; mode = 'windowed'; resolution = '1300x1300' }

function Say([string]$t) {
    Add-Content -Path $script:LogFile -Value ("{0}  {1}" -f (Get-Date -Format 'HH:mm:ss'), $t) -Encoding UTF8
    & $script:Say $t
}

# 'ok' / 'other' / 'none': is MW4Engine DX11 there, and is it the build MW4VR was made for?
function Get-HookState([string]$Game) {
    $p = Join-Path $Game 'mw4hook.dll'
    if (-not (Test-Path $p)) { return 'none' }
    $fs = [IO.File]::Open($p, 'Open', 'Read', 'ReadWrite')
    try {
        $b = New-Object byte[] 4096
        [void]$fs.Read($b, 0, 4096)
    } finally { $fs.Close() }
    $pe = [BitConverter]::ToInt32($b, 0x3c)
    if ($pe -lt 0 -or $pe -gt 4000) { return 'other' }
    if ([BitConverter]::ToUInt32($b, $pe + 8) -eq $KnownHookStamp) { return 'ok' }
    return 'other'
}

function Test-Game([string]$Game) { return ($Game -and (Test-Path (Join-Path $Game 'MW4Mercs.exe'))) }

# copies a file; one that is there and differs is kept beside it as <name>.before_mw4vr (once)
function Copy-Kept([string]$From, [string]$To) {
    if (Test-Path $To) {
        if ((Get-FileHash $From).Hash -eq (Get-FileHash $To).Hash) { Clear-WebMark $To; return }
        $keep = "$To.before_mw4vr"
        if (-not (Test-Path $keep)) { Copy-Item $To $keep }
    }
    Copy-Item $From $To -Force
    Clear-WebMark $To
}

# A package unpacked from a downloaded zip carries Windows' "from the internet" mark on every file, and a copy
# takes it along: the start script then asks "publisher could not be verified" at every start. The files the
# player has just had installed lose the mark.
function Clear-WebMark([string]$File) {
    try { Unblock-File -LiteralPath $File -ErrorAction Stop } catch { }
}

# 'v11.38' from a d3d11.dll of MW4VR's ('' = the file is not there or not MW4VR's)
function Get-ModVersion([string]$Dll) {
    if (-not (Test-Path $Dll)) { return '' }
    $fs = [IO.File]::Open($Dll, 'Open', 'Read', 'ReadWrite')
    try {
        $b = New-Object byte[] $fs.Length
        [void]$fs.Read($b, 0, $b.Length)
    } finally { $fs.Close() }
    $m = [regex]::Match([Text.Encoding]::GetEncoding(28591).GetString($b), 'MW4VR (v\d+\.\d+)')
    if ($m.Success) { return $m.Groups[1].Value }
    return ''
}

# What the setup put there, by file: <game>\mw4vr_setup_state.txt, lines "name=hash". A settings file that is still
# as the setup left it is not the player's work, so a later package's newer one may take its place.
function Read-State([string]$Game) {
    $s = @{}
    $p = Join-Path $Game 'mw4vr_setup_state.txt'
    if (Test-Path $p) { foreach ($l in Get-Content $p) { $i = $l.LastIndexOf('='); if ($i -gt 0) { $s[$l.Substring(0, $i)] = $l.Substring($i + 1) } } }
    return $s
}
function Write-State([string]$Game, $State) {
    $lines = @($State.Keys | Sort-Object | ForEach-Object { "$_=$($State[$_])" })
    Set-Content -Path (Join-Path $Game 'mw4vr_setup_state.txt') -Value $lines -Encoding Default
}
# a default settings file: 'new' (was not there), 'updated' (was as the setup left it, the package's is newer),
# 'same', or 'kept' (the player changed it)
function Update-Default([string]$From, [string]$To, [string]$Key, $State) {
    $h = (Get-FileHash $From).Hash
    if (-not (Test-Path $To)) { Copy-Item $From $To; $State[$Key] = $h; return 'new' }
    $have = (Get-FileHash $To).Hash
    if ($have -eq $h) { $State[$Key] = $h; return 'same' }
    if ($State[$Key] -and $State[$Key] -eq $have) { Copy-Item $From $To -Force; $State[$Key] = $h; return 'updated' }
    return 'kept'
}

# keys the package's mw4vr.ini has and the player's has not are added to the player's (with the comment lines
# above them), at the end of their section; nothing of the player's is changed. -> the keys added
function Merge-Ini([string]$From, [string]$To) {
    $enc = [Text.Encoding]::Default
    $new = [IO.File]::ReadAllLines($From, $enc)
    $old = New-Object Collections.Generic.List[string]
    $old.AddRange([IO.File]::ReadAllLines($To, $enc))
    $have = @{}; $sec = ''
    foreach ($l in $old) {
        if ($l -match '^\s*\[(.+?)\]') { $sec = $Matches[1].ToLower() }
        elseif ($l -match '^\s*([^;=\s][^=]*?)\s*=') { $have["$sec|$($Matches[1].ToLower())"] = 1 }
    }
    $added = @(); $sec = ''; $note = @()
    foreach ($l in $new) {
        if ($l -match '^\s*\[(.+?)\]') { $sec = $Matches[1].ToLower(); $note = @(); continue }
        if ($l -match '^\s*;') { $note += $l; continue }
        if ($l -match '^\s*([^;=\s][^=]*?)\s*=') {
            $k = $Matches[1]
            if (-not $have["$sec|$($k.ToLower())"]) {
                # where the section ends in the player's file (no such section: a new one at the end)
                $at = -1; $in = $false
                for ($i = 0; $i -lt $old.Count; $i++) {
                    if ($old[$i] -match '^\s*\[(.+?)\]') {
                        if ($in) { $at = $i; break }
                        $in = ($Matches[1].ToLower() -eq $sec)
                    }
                }
                if (-not $in) { $old.Add("[$sec]"); $at = $old.Count }
                elseif ($at -lt 0) { $at = $old.Count }
                while ($at -gt 0 -and $old[$at - 1].Trim() -eq '') { $at-- }
                $old.InsertRange($at, [string[]](@($note) + $l))
                $have["$sec|$($k.ToLower())"] = 1
                $added += $k
            }
        }
        $note = @()
    }
    if ($added.Count) { [IO.File]::WriteAllLines($To, $old, $enc) }
    return $added
}

# mechs the package's mw4vr_mechs.ini knows by name (model=) and the player's does not are added to the player's
# as new profiles; the player's own profiles stay as they are. -> how many
function Merge-Mechs([string]$From, [string]$To) {
    $enc = [Text.Encoding]::Default
    $read = {
        param($p)
        $secs = [ordered]@{}; $cur = $null
        foreach ($l in [IO.File]::ReadAllLines($p, $enc)) {
            if ($l -match '^\s*\[(.+?)\]') { $cur = $Matches[1].ToLower(); $secs[$cur] = New-Object Collections.Generic.List[string] }
            elseif ($cur -and $l.Trim()) { $secs[$cur].Add($l) }
        }
        return $secs
    }
    $model = { param($lines) foreach ($l in $lines) { if ($l -match '^\s*model\s*=\s*(\S+)') { return $Matches[1].ToLower() } }; return '' }
    $new = & $read $From
    $old = & $read $To
    $count = 0
    if ($old.Contains('mechs')) { foreach ($l in $old['mechs']) { if ($l -match '^\s*count\s*=\s*(\d+)') { $count = [int]$Matches[1] } } }
    $known = @{}
    for ($i = 1; $i -le $count; $i++) { if ($old.Contains("mech_$i")) { $m = & $model $old["mech_$i"]; if ($m) { $known[$m] = 1 } } }
    $add = New-Object Collections.Generic.List[string]
    $n = 0
    foreach ($k in $new.Keys) {
        if ($k -notmatch '^mech_\d+$') { continue }
        $m = & $model $new[$k]
        if (-not $m -or $known[$m]) { continue }
        $count++; $n++; $known[$m] = 1
        $add.Add("[mech_$count]")
        $add.AddRange($new[$k])
    }
    if (-not $n) { return 0 }
    $out = New-Object Collections.Generic.List[string]
    $sec = ''; $done = $false
    foreach ($l in [IO.File]::ReadAllLines($To, $enc)) {
        if ($l -match '^\s*\[(.+?)\]') { $sec = $Matches[1].ToLower() }
        if ($sec -eq 'mechs' -and $l -match '^\s*count\s*=') { $out.Add("count=$count"); $done = $true } else { $out.Add($l) }
    }
    if (-not $done) { $out.Insert(0, "count=$count"); $out.Insert(0, '[mechs]') }
    $out.AddRange($add)
    Copy-Item $To "$To.before_update" -Force
    [IO.File]::WriteAllLines($To, $out, $enc)
    return $n
}

# First time and every later one (an update): the program files are replaced, the player's settings never -
# they only get what is new (keys, mechs, cockpit settings the player has not touched).
function Install-Mod([string]$Game, [bool]$Dinput) {
    $src = Join-Path $script:Pkg 'game'
    $was = Get-ModVersion (Join-Path $Game 'd3d11.dll')
    $now = Get-ModVersion (Join-Path $src 'd3d11.dll')
    Copy-Kept (Join-Path $src 'd3d11.dll') (Join-Path $Game 'd3d11.dll')
    if ($Dinput) { Copy-Kept (Join-Path $src 'dinput.dll') (Join-Path $Game 'dinput.dll') }
    foreach ($f in 'mw4vr_cockpit_map.ini', 'mw4vr_start.cmd', 'mw4vr_start.ps1', 'mw4vr_readme.txt') {
        Copy-Kept (Join-Path $src $f) (Join-Path $Game $f)
    }
    $state = Read-State $Game
    $ini = Join-Path $Game 'mw4vr.ini'
    if (Test-Path $ini) {
        $keys = @(Merge-Ini (Join-Path $src 'mw4vr.ini') $ini)
        if ($keys.Count) { Say "mw4vr.ini: your settings kept, new ones added: $($keys -join ', ')." }
        else { Say 'mw4vr.ini: your settings kept, nothing new to add.' }
    } else {
        Copy-Item (Join-Path $src 'mw4vr.ini') $ini
    }
    $mechs = Join-Path $Game 'mw4vr_mechs.ini'
    if (-not (Test-Path $mechs)) { Copy-Item (Join-Path $src 'mw4vr_mechs.ini') $mechs }
    else {
        $m = Merge-Mechs (Join-Path $src 'mw4vr_mechs.ini') $mechs
        if ($m) { Say "mw4vr_mechs.ini: $m mechs new (the file as it was: mw4vr_mechs.ini.before_update)." }
    }
    $cp = Join-Path $Game 'mw4vr_cockpits'
    New-Item -ItemType Directory $cp -Force | Out-Null
    $n = @{ new = 0; updated = 0; same = 0; kept = 0 }
    Get-ChildItem (Join-Path $src 'mw4vr_cockpits') -Filter *.ini -ErrorAction SilentlyContinue | ForEach-Object {
        $n[(Update-Default $_.FullName (Join-Path $cp $_.Name) "mw4vr_cockpits\$($_.Name)" $state)]++
    }
    Write-State $Game $state
    $what = if (-not $was) { "MW4VR $now is in the game's folder" } elseif ($was -eq $now) { "MW4VR $now is in the game's folder again" } else { "MW4VR updated: $was -> $now" }
    Say "$what (cockpit settings: $($n.new) new, $($n.updated) updated, $($n.kept) of your own kept)."
}

# sets the keys MW4VR needs in mw4hook.ini (the file as it was: mw4hook.ini.before_mw4vr)
function Set-HookIni([string]$Game) {
    $p = Join-Path $Game 'mw4hook.ini'
    if (-not (Test-Path $p)) { Say 'mw4hook.ini not found - start the game once with MW4Engine DX11, then run the setup again.'; return $false }
    $lines = @(Get-Content $p)
    $changed = @(); $missing = @()
    foreach ($k in $HookKeys.Keys) {
        $want = "$k=$($HookKeys[$k])"
        $hit = $false
        for ($i = 0; $i -lt $lines.Count; $i++) {
            if ($lines[$i] -match "^\s*$k\s*=") {
                $hit = $true
                if ($lines[$i].Trim() -ne $want) { $changed += "$($lines[$i].Trim()) -> $want"; $lines[$i] = $want }
            }
        }
        if (-not $hit) { $missing += $want }
    }
    if ($changed.Count) {
        if (-not (Test-Path "$p.before_mw4vr")) { Copy-Item $p "$p.before_mw4vr" }
        Set-Content -Path $p -Value $lines -Encoding Default
        Say "mw4hook.ini: $($changed -join '; ')"
    } else { Say 'mw4hook.ini: the settings are right already.' }
    if ($missing.Count) { Say "mw4hook.ini has no line for: $($missing -join ', ') - add them by hand." }
    return $true
}

function Install-Trees([string]$Game) {
    $src = Join-Path $script:Pkg 'trees'
    if (-not (Test-Path (Join-Path $src 'clutter_trees.txt'))) { Say 'This package has no trees.'; return }
    $clutter = Join-Path $Game 'Content\Clutter\clutter.txt'
    if (-not (Test-Path $clutter)) { Say 'Content\Clutter\clutter.txt not found (MW4Engine DX11 with terrain clutter is needed) - trees left out.'; return }
    Copy-Item (Join-Path $src 'Content') $Game -Recurse -Force
    $old = [IO.File]::ReadAllText($clutter, [Text.Encoding]::Default)
    $new = [IO.File]::ReadAllText((Join-Path $src 'clutter_trees.txt'), [Text.Encoding]::Default)
    if (-not (Test-Path "$clutter.before_mw4vr")) { Copy-Item $clutter "$clutter.before_mw4vr" }
    $at = $old.IndexOf('; MW4VR trees')
    if ($at -ge 0) { $old = $old.Substring(0, $at) }
    [IO.File]::WriteAllText($clutter, $old.TrimEnd() + "`r`n`r`n" + $new.Trim() + "`r`n", [Text.Encoding]::Default)
    Say 'The trees are installed (clutter.txt as it was: clutter.txt.before_mw4vr).'
}

function New-StartShortcut([string]$Game) {
    $sh = New-Object -ComObject WScript.Shell
    $lnk = $sh.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Desktop')) 'MechWarrior 4 VR.lnk'))
    $lnk.TargetPath = Join-Path $Game 'mw4vr_start.cmd'
    $lnk.WorkingDirectory = $Game
    $lnk.IconLocation = (Join-Path $Game 'MW4Mercs.exe') + ',0'
    $lnk.WindowStyle = 7
    $lnk.Save()
    Say 'Shortcut "MechWarrior 4 VR" is on the desktop.'
}

# where MechWarrior 5: Mercenaries keeps its paks ('' = not found): Epic's manifests, Steam's libraries
function Find-Mw5Paks {
    $cand = @()
    $man = Join-Path $env:ProgramData 'Epic\EpicGamesLauncher\Data\Manifests'
    if (Test-Path $man) {
        Get-ChildItem $man -Filter *.item | ForEach-Object {
            try {
                $j = Get-Content $_.FullName -Raw | ConvertFrom-Json
                if ("$($j.DisplayName)" -match 'MechWarrior 5' -and "$($j.DisplayName)" -notmatch 'Clans') { $cand += $j.InstallLocation }
            } catch { }
        }
    }
    try {
        $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
        $vdf = Join-Path $steam 'steamapps\libraryfolders.vdf'
        $libs = @($steam)
        if (Test-Path $vdf) { $libs += [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"') | ForEach-Object { $_.Groups[1].Value -replace '\\\\', '\' } }
        foreach ($l in $libs) { $cand += Join-Path $l 'steamapps\common\MechWarrior 5 Mercenaries' }
    } catch { }
    foreach ($c in $cand) {
        if (-not $c) { continue }
        $p = Join-Path $c 'MW5Mercs\Content\Paks'
        if (Test-Path $p) { return $p }
    }
    return ''
}

# the Python to use: @{Exe; Arg} or $null; $Modules: are numpy and Pillow there?
function Find-Python([ref]$Modules) {
    $Modules.Value = $false
    # (not 'Stop' in here: with it, what a program writes to its error output - Python saying a module is missing -
    # ends the try, and a Python without numpy counted as no Python at all)
    $ErrorActionPreference = 'Continue'
    foreach ($c in @(@('python', ''), @('py', '-3'), @('python3', ''))) {
        # every one of that name on the PATH: the first may be Windows' placeholder that only points to the Store
        # (it answers nothing), a real Python from the Store lies in the same WindowsApps folder and does answer
        foreach ($cmd in @(Get-Command $c[0] -All -CommandType Application -ErrorAction SilentlyContinue)) {
            $a = @(); if ($c[1]) { $a += $c[1] }
            try {
                $v = & $cmd.Source @a -c 'import sys; print(sys.version_info[0])' 2>$null
                if ("$v".Trim() -ne '3') { continue }
                & $cmd.Source @a -c 'import numpy, PIL' 2>$null | Out-Null
                $Modules.Value = ($LASTEXITCODE -eq 0)
                return @{ Exe = $cmd.Source; Arg = $c[1] }
            } catch { }
        }
    }
    return $null
}

# The newest Python 3 the cockpit tools can use: the highest 3.x that numpy AND Pillow have ready-made Windows
# packages for (asked of pypi.org) and that winget offers. The very newest Python is not always it: for a while
# after its release the packages are missing and pip would try - and fail - to build them. -> '3.14', or the
# tried and tested '3.12' when nothing can be asked.
function Get-PythonToInstall {
    $best = '3.12'
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $sets = @()
        foreach ($pkg in 'numpy', 'pillow') {
            $j = Invoke-RestMethod -Uri "https://pypi.org/pypi/$pkg/json" -TimeoutSec 15
            $sets += , @($j.urls | ForEach-Object { if ($_.filename -match '-cp3(\d+)-cp3\1-win_amd64\.whl$') { [int]$Matches[1] } } | Sort-Object -Unique)
        }
        $both = @($sets[0] | Where-Object { $sets[1] -contains $_ } | Sort-Object -Descending)
        foreach ($minor in $both) {
            if ($minor -le 12) { break }
            & winget show -e --id "Python.Python.3.$minor" --source winget --accept-source-agreements 2>$null | Out-Null
            if ($LASTEXITCODE -eq 0) { $best = "3.$minor"; break }
        }
    } catch { }
    return $best
}

# The folders chosen in the window, kept for the next start of the setup (also of a later package's):
# %APPDATA%\MW4VR\setup.txt, lines "name=path"
function Get-PathsFile { return (Join-Path $env:APPDATA 'MW4VR\setup.txt') }
function Read-Paths {
    $s = @{}
    $p = Get-PathsFile
    if (Test-Path $p) { foreach ($l in Get-Content $p -Encoding UTF8) { $i = $l.IndexOf('='); if ($i -gt 0) { $s[$l.Substring(0, $i)] = $l.Substring($i + 1) } } }
    return $s
}
function Write-Paths($Paths) {
    try {
        $p = Get-PathsFile
        New-Item -ItemType Directory (Split-Path -Parent $p) -Force | Out-Null
        $lines = @($Paths.Keys | Sort-Object | Where-Object { $Paths[$_] } | ForEach-Object { "$_=$($Paths[$_])" })
        [IO.File]::WriteAllLines($p, [string[]]$lines, (New-Object Text.UTF8Encoding($false)))
    } catch { }
}

if ($NoGui) { return }

# ---- the window ---------------------------------------------------------------------------------
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$f = New-Object Windows.Forms.Form
$f.Text = 'MW4VR Setup'
$f.ClientSize = New-Object Drawing.Size(720, 770)
$f.StartPosition = 'CenterScreen'
$f.FormBorderStyle = 'FixedDialog'
$f.MaximizeBox = $false
$f.Font = New-Object Drawing.Font('Segoe UI', 9)
$script:y = 12
function Add-Label([string]$t, [int]$x = 14, [int]$w = 690, [bool]$bold = $false) {
    $l = New-Object Windows.Forms.Label
    $l.Text = $t; $l.Location = New-Object Drawing.Point($x, $script:y); $l.Size = New-Object Drawing.Size($w, 18)
    if ($bold) { $l.Font = New-Object Drawing.Font('Segoe UI', 9, [Drawing.FontStyle]::Bold) }
    $f.Controls.Add($l); $script:y += 20; return $l
}
function Add-PathRow([string]$text) {
    $t = New-Object Windows.Forms.TextBox
    $t.Location = New-Object Drawing.Point(14, $script:y); $t.Size = New-Object Drawing.Size(600, 23); $t.Text = $text
    $b = New-Object Windows.Forms.Button
    $b.Text = 'Browse...'; $b.Location = New-Object Drawing.Point(620, ($script:y - 1)); $b.Size = New-Object Drawing.Size(86, 25)
    $f.Controls.AddRange(@($t, $b)); $script:y += 28
    return @($t, $b)
}
function Add-Check([string]$t, [bool]$on, [int]$x = 14) {
    $c = New-Object Windows.Forms.CheckBox
    $c.Text = $t; $c.Checked = $on; $c.Location = New-Object Drawing.Point($x, $script:y); $c.Size = New-Object Drawing.Size(690, 20)
    $f.Controls.Add($c); $script:y += 22; return $c
}
function Pick-Folder([Windows.Forms.TextBox]$t, [string]$what) {
    $d = New-Object Windows.Forms.FolderBrowserDialog
    $d.Description = $what
    if ($t.Text -and (Test-Path $t.Text)) { $d.SelectedPath = $t.Text }
    if ($d.ShowDialog($f) -eq 'OK') { $t.Text = $d.SelectedPath }
}

[void](Add-Label '1. The game' 14 690 $true)
[void](Add-Label "The folder of MechWarrior 4: Mercenaries (with MW4Mercs.exe and MW4Engine DX11's mw4hook.dll):")
$guess = ''
$saved = Read-Paths
foreach ($g in @((Split-Path -Parent $script:Pkg), (Split-Path -Parent (Split-Path -Parent $script:Pkg)), $saved['game'])) { if ($g -and (Test-Game $g)) { $guess = $g } }
$tGame, $bGame = Add-PathRow $guess
$lGame = Add-Label ''
$script:y += 6
[void](Add-Label '2. What to install' 14 690 $true)
$cMod = Add-Check 'MW4VR (the mod, its settings, the start script, the readme)' $true
$cDinput = Add-Check 'Start-up fix dinput.dll (the game dying or hanging at start-up on some PCs)' $true
$cHook = Add-Check "Set MW4Engine's settings for VR in mw4hook.ini (antihack=0, fov=130, vsync=0, taa=0, windowed 1300x1300)" $true
$cTrees = Add-Check 'More trees and shrubs for the terrain (needs MW4Engine''s terrain clutter)' (Test-Path (Join-Path $script:Pkg 'trees\clutter_trees.txt'))
$cTrees.Enabled = $cTrees.Checked
$cLink = Add-Check 'Shortcut "MechWarrior 4 VR" on the desktop' $true
$script:y += 6
[void](Add-Label '3. MechWarrior 5 cockpits (optional - only if you own MechWarrior 5: Mercenaries)' 14 690 $true)
$cMw5 = Add-Check 'Export my MechWarrior 5 cockpits and convert them for MW4VR (about 20 minutes, 6 GB while it runs)' $false
$cMods = Add-Check 'Also the cockpits of the MechWarrior 5 mods installed there (some more minutes)' $false 32
[void](Add-Label '...\MW5Mercs\Content\Paks of MechWarrior 5: Mercenaries:')
$tPaks, $bPaks = Add-PathRow $(if ($saved['paks'] -and (Test-Path $saved['paks'])) { $saved['paks'] } else { Find-Mw5Paks })
$lMw5 = Add-Label ''
$bEpic = New-Object Windows.Forms.Button
$bEpic.Text = 'MechWarrior 5 in the Epic Games Store'; $bEpic.Location = New-Object Drawing.Point(14, $script:y); $bEpic.Size = New-Object Drawing.Size(250, 25)
$bSteam = New-Object Windows.Forms.Button
$bSteam.Text = 'MechWarrior 5 on Steam'; $bSteam.Location = New-Object Drawing.Point(272, $script:y); $bSteam.Size = New-Object Drawing.Size(180, 25)
$f.Controls.AddRange(@($bEpic, $bSteam)); $script:y += 32
[void](Add-Label 'UE Viewer (umodel_64.exe) - a free tool by Gildor; download and unpack it, then choose the exe:')
$um = Join-Path $script:Pkg 'cockpit_tools\umodel\umodel_64.exe'
$tUm, $bUm = Add-PathRow $(if (Test-Path $um) { $um } elseif ($saved['umodel'] -and (Test-Path $saved['umodel'])) { $saved['umodel'] } else { '' })
$bGet = New-Object Windows.Forms.Button
$bGet.Text = 'Open the UE Viewer download page'; $bGet.Location = New-Object Drawing.Point(14, $script:y); $bGet.Size = New-Object Drawing.Size(230, 25)
$f.Controls.Add($bGet); $script:y += 30
$lPy = Add-Label ''
# only while Python is missing: get it (winget, or its page), then look again
$bPyGet = New-Object Windows.Forms.Button
$bPyGet.Text = 'Install Python 3 now'; $bPyGet.Location = New-Object Drawing.Point(14, $script:y); $bPyGet.Size = New-Object Drawing.Size(170, 25)
$bPySite = New-Object Windows.Forms.Button
$bPySite.Text = 'Open python.org'; $bPySite.Location = New-Object Drawing.Point(192, $script:y); $bPySite.Size = New-Object Drawing.Size(130, 25)
$bPyAgain = New-Object Windows.Forms.Button
$bPyAgain.Text = 'Look again'; $bPyAgain.Location = New-Object Drawing.Point(330, $script:y); $bPyAgain.Size = New-Object Drawing.Size(110, 25)
$f.Controls.AddRange(@($bPyGet, $bPySite, $bPyAgain)); $script:y += 30
$cKeep = Add-Check 'Delete the exported files (2 GB) when the cockpits are converted' $true
$script:y += 6
$bGo = New-Object Windows.Forms.Button
$bGo.Text = 'Install'; $bGo.Location = New-Object Drawing.Point(14, $script:y); $bGo.Size = New-Object Drawing.Size(120, 30)
$bClose = New-Object Windows.Forms.Button
$bClose.Text = 'Close'; $bClose.Location = New-Object Drawing.Point(586, $script:y); $bClose.Size = New-Object Drawing.Size(120, 30)
$f.Controls.AddRange(@($bGo, $bClose)); $script:y += 38
$log = New-Object Windows.Forms.TextBox
$log.Multiline = $true; $log.ReadOnly = $true; $log.ScrollBars = 'Vertical'
$log.Location = New-Object Drawing.Point(14, $script:y); $log.Size = New-Object Drawing.Size(692, (770 - $script:y - 12))
$log.Font = New-Object Drawing.Font('Consolas', 8.5)
$f.Controls.Add($log)
$script:Say = { param($t) $log.AppendText($t + "`r`n") }

$script:Py = $null
$script:PyMods = $false
$script:ModsDir = ''
function Show-Game {
    $g = $tGame.Text.Trim()
    if (-not (Test-Game $g)) { $lGame.Text = 'MW4Mercs.exe is not in this folder.'; $lGame.ForeColor = 'Firebrick'; return }
    # an update? the button and the line say so
    $was = Get-ModVersion (Join-Path $g 'd3d11.dll')
    $now = Get-ModVersion (Join-Path $script:Pkg 'game\d3d11.dll')
    $bGo.Text = if ($was) { 'Update' } else { 'Install' }
    $ver = if (-not $was) { '' } elseif ($was -eq $now) { " MW4VR $was is installed (the same as this package)." } else { " MW4VR $was is installed - this package: $now. Your settings are kept." }
    switch (Get-HookState $g) {
        'ok' { $lGame.Text = "Found: the game and MW4Engine DX11 in the build MW4VR was made for.$ver"; $lGame.ForeColor = 'DarkGreen' }
        'other' { $lGame.Text = 'Found the game. MW4Engine DX11 is another build than MW4VR was made for: parts of MW4VR will stay off.' + $ver; $lGame.ForeColor = 'DarkOrange' }
        default { $lGame.Text = 'Found the game, but no mw4hook.dll: install MW4Engine DX11 first.'; $lGame.ForeColor = 'Firebrick' }
    }
}
function Show-Python {
    $m = $false
    $script:Py = Find-Python ([ref]$m)
    $script:PyMods = $m
    # red: something for the player to do; green: nothing (missing packages are the setup's work)
    if (-not $script:Py) { $lPy.Text = 'Python 3 is MISSING - needed for the cockpits only. Install it (with "Add to PATH"), then "Look again":'; $lPy.ForeColor = 'Firebrick' }
    elseif (-not $m) { $lPy.Text = 'Python 3 found. The setup will add its packages numpy and Pillow by itself when it converts.'; $lPy.ForeColor = 'DarkGreen' }
    else { $lPy.Text = 'Python 3 with numpy and Pillow found.'; $lPy.ForeColor = 'DarkGreen' }
    $bPyGet.Visible = $bPySite.Visible = $bPyAgain.Visible = (-not $script:Py)
    $bPyGet.Enabled = [bool](Get-Command winget -ErrorAction SilentlyContinue)
}
# a program installed while the setup runs is on the PATH of new processes only: read the PATH anew
function Update-Path {
    $env:PATH = ([Environment]::GetEnvironmentVariable('Path', 'Machine'), [Environment]::GetEnvironmentVariable('Path', 'User') | Where-Object { $_ }) -join ';'
}
# without MechWarrior 5: Mercenaries on this PC the cockpits cannot be chosen - the stores' pages are a click away
function Show-Mw5 {
    $p = $tPaks.Text.Trim()
    $have = [bool]($p -and (Test-Path $p) -and @(Get-ChildItem $p -Filter *.pak -ErrorAction SilentlyContinue).Count)
    $cMw5.Enabled = $have
    if (-not $have) { $cMw5.Checked = $false }
    if ($have) { $lMw5.Text = 'MechWarrior 5: Mercenaries found.'; $lMw5.ForeColor = 'DarkGreen' }
    else { $lMw5.Text = 'MechWarrior 5: Mercenaries was not found on this PC. Buy or install it, then choose its Paks folder or start the setup again:'; $lMw5.ForeColor = 'DarkOrange' }
    $bEpic.Visible = $bSteam.Visible = (-not $have) # the stores only for who has not got it
    # mods: the folder Mods beside Content, with something in it
    $script:ModsDir = ''
    if ($have) {
        $m = Join-Path (Split-Path -Parent (Split-Path -Parent $p.TrimEnd(''))) 'Mods'
        if ((Test-Path $m) -and @(Get-ChildItem $m -Directory -ErrorAction SilentlyContinue).Count) { $script:ModsDir = $m }
    }
    $cMods.Enabled = [bool]$script:ModsDir -and $cMw5.Checked
    if (-not $cMods.Enabled) { $cMods.Checked = $false }
}
$bPySite.Add_Click({ Start-Process 'https://www.python.org/downloads/windows/' })
$bPyAgain.Add_Click({ Update-Path; Show-Python; if ($script:Py) { Say "Python found: $($script:Py.Exe)" } else { Say 'Python 3 is still not found.' } })
$bPyGet.Add_Click({
    # Windows' own package manager, in a console of its own so that its questions and progress can be seen
    $bPyGet.Enabled = $false
    Say 'Looking for the newest Python 3 that numpy and Pillow are ready for ...'
    [Windows.Forms.Application]::DoEvents()
    $ErrorActionPreference = 'Continue' # (winget's and the network's complaints are not the end of this)
    $ver = Get-PythonToInstall
    Say "Installing Python $ver with winget - follow its window; when it has closed, the setup looks again."
    try {
        $p = Start-Process winget -ArgumentList 'install', '-e', '--id', "Python.Python.$ver", '--scope', 'user', '--accept-package-agreements', '--accept-source-agreements' -PassThru
        while (-not $p.HasExited) { [Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 150 }
        Say "winget has finished (code $($p.ExitCode))."
    } catch { Say "winget could not be started: $($_.Exception.Message) - use python.org instead." }
    Update-Path
    Show-Python
    if ($script:Py) { Say "Python found: $($script:Py.Exe)" } else { Say 'Python 3 is still not found - install it from python.org ("Add to PATH"), then "Look again".' }
})
$tPaks.Add_TextChanged({ Show-Mw5 })
$cMw5.Add_CheckedChanged({ Show-Mw5 })
$bEpic.Add_Click({ Start-Process 'https://store.epicgames.com/p/mechwarrior-5' })
$bSteam.Add_Click({ Start-Process 'https://store.steampowered.com/app/784080' })
$tGame.Add_TextChanged({ Show-Game })
$bGame.Add_Click({ Pick-Folder $tGame 'The folder of MechWarrior 4: Mercenaries' })
$bPaks.Add_Click({ Pick-Folder $tPaks 'MW5Mercs\Content\Paks of MechWarrior 5: Mercenaries' })
$bUm.Add_Click({
    $d = New-Object Windows.Forms.OpenFileDialog
    $d.Filter = 'UE Viewer (umodel*.exe)|umodel*.exe'
    if ($d.ShowDialog($f) -eq 'OK') { $tUm.Text = $d.FileName }
})
$bGet.Add_Click({ Start-Process 'https://www.gildor.org/en/projects/umodel' })
$bClose.Add_Click({ $f.Close() })
# what stands in the three fields is kept when the installation starts and when the window closes
function Save-Fields {
    if ($env:MW4VR_SETUP_TEST -and -not $env:MW4VR_SETUP_TEST_SAVE) { return } # (a test leaves the player's file alone)
    $p = Read-Paths
    $g = $tGame.Text.Trim(); if (Test-Game $g) { $p['game'] = $g }
    $k = $tPaks.Text.Trim(); if ($k -and (Test-Path $k)) { $p['paks'] = $k }
    $u = $tUm.Text.Trim(); if ($u -and (Test-Path $u)) { $p['umodel'] = $u }
    Write-Paths $p
}
$f.Add_FormClosing({ Save-Fields })

$script:Job = $null
$script:JobOut = ''
$script:JobPos = 0
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 700
$timer.Add_Tick({
    if (-not $script:Job) { return }
    try {
        $fs = [IO.File]::Open($script:JobOut, 'Open', 'Read', 'ReadWrite')
        try {
            [void]$fs.Seek($script:JobPos, 'Begin')
            $r = New-Object IO.StreamReader($fs)
            $new = $r.ReadToEnd()
            $script:JobPos = $fs.Position
        } finally { $fs.Close() }
        if ($new) { $log.AppendText(($new -replace "(?<!`r)`n", "`r`n")) }
    } catch { }
    if ($script:Job.HasExited) {
        $code = $script:Job.ExitCode
        $script:Job = $null
        $timer.Stop()
        if ($code -eq 0) { Say 'The cockpits are ready. In a battle: Ctrl+F11 switches them, Ctrl+Alt+F11 chooses another one.' }
        else { Say "The cockpits were NOT finished (step $code failed) - see the lines above. The rest of the installation is done." }
        Say 'Finished. Start the game with the shortcut or mw4vr_start.cmd, the headset connected.'
        $bGo.Enabled = $true
    }
})

$bGo.Add_Click({
    $g = $tGame.Text.Trim()
    if (-not (Test-Game $g)) { [void][Windows.Forms.MessageBox]::Show($f, "Choose the game's folder first.", 'MW4VR Setup'); return }
    if (Get-Process MW4Mercs -ErrorAction SilentlyContinue) { [void][Windows.Forms.MessageBox]::Show($f, 'The game is running - close it first.', 'MW4VR Setup'); return }
    if ($cMw5.Checked) {
        Show-Python
        $why = ''
        if (-not (Test-Path (Join-Path $tPaks.Text.Trim() '.'))) { $why = 'The Paks folder of MechWarrior 5 was not found.' }
        elseif (-not ($tUm.Text.Trim() -and (Test-Path $tUm.Text.Trim()))) { $why = 'Choose umodel_64.exe (UE Viewer) first.' }
        elseif (-not $script:Py) { $why = 'Python 3 is needed for the cockpits.' }
        elseif ((Get-PSDrive ([IO.Path]::GetPathRoot($g).Substring(0, 1))).Free -lt 7GB) { $why = "The game's drive needs 7 GB of free space for the cockpits." }
        if ($why) { [void][Windows.Forms.MessageBox]::Show($f, $why, 'MW4VR Setup'); return }
    }
    $bGo.Enabled = $false
    Save-Fields
    try {
        Say "Installing into $g"
        if ($cMod.Checked) { Install-Mod $g $cDinput.Checked }
        elseif ($cDinput.Checked) { Copy-Kept (Join-Path $script:Pkg 'game\dinput.dll') (Join-Path $g 'dinput.dll'); Say 'dinput.dll installed.' }
        if ($cHook.Checked) { [void](Set-HookIni $g) }
        if ($cTrees.Checked) { Install-Trees $g }
        if ($cLink.Checked) { New-StartShortcut $g }
    } catch {
        Say "FAILED: $($_.Exception.Message)"
        $bGo.Enabled = $true
        return
    }
    if (-not $cMw5.Checked) {
        Say 'Finished. Start the game with the shortcut or mw4vr_start.cmd, the headset connected.'
        $bGo.Enabled = $true
        return
    }
    Say 'Exporting and converting the MechWarrior 5 cockpits - this takes a while ...'
    $script:JobOut = Join-Path $env:TEMP 'mw4vr_setup_cockpits.txt'
    Set-Content $script:JobOut '' -Encoding UTF8
    $script:JobPos = 0
    # Each path between quotes. One typed with a \ at its end ("...\Paks\") would end in \" - which the program
    # started reads as a quote inside the argument, so that the arguments ran together: its last backslashes doubled.
    function Quote-Arg([string]$s) { '"' + ($s -replace '(\\+)$', '$1$1') + '"' }
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Quote-Arg (Join-Path $script:Pkg 'mw4vr_setup_cockpits.ps1')),
        '-Tools', (Quote-Arg (Join-Path $script:Pkg 'cockpit_tools')), '-Paks', (Quote-Arg $tPaks.Text.Trim()), '-Umodel', (Quote-Arg $tUm.Text.Trim()),
        '-Game', (Quote-Arg $g), '-Python', (Quote-Arg $script:Py.Exe), '-Log', (Quote-Arg $script:JobOut))
    if ($script:Py.Arg) { $a += @('-PythonArg', $script:Py.Arg) }
    if (-not $script:PyMods) { $a += '-Pip' }
    if ($cKeep.Checked) { $a += '-DeleteExport' }
    if ($cMods.Checked -and $script:ModsDir) { $a += @('-Mods', (Quote-Arg $script:ModsDir)) }
    # (Windows' own PowerShell by its full path, not whatever is called powershell in the folder the setup runs in)
    $script:Job = Start-Process (Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe') -ArgumentList $a -WindowStyle Hidden -PassThru
    $timer.Start()
})

Show-Game
Show-Mw5
Show-Python
Say "MW4VR setup. Its log: $script:LogFile"
if ($env:MW4VR_SETUP_TEST) { # (a test: the window is built and shown, then closed again)
    $f.Add_Shown({
        if ($env:MW4VR_SETUP_TEST_NOMW5) { $tPaks.Text = '' } # (as on a PC without MechWarrior 5)
        Write-Host "window ok: $($lGame.Text) | $($lPy.Text) | paks '$($tPaks.Text)' | $($lMw5.Text) | cockpits can be chosen: $($cMw5.Enabled)"
        if ($env:MW4VR_SETUP_TEST -like '*.png') { # its picture, to look the layout over
            $bmp = New-Object Drawing.Bitmap($f.Width, $f.Height)
            $f.DrawToBitmap($bmp, (New-Object Drawing.Rectangle(0, 0, $f.Width, $f.Height)))
            $bmp.Save($env:MW4VR_SETUP_TEST)
        }
        $f.Close()
    })
}
[void]$f.ShowDialog()
