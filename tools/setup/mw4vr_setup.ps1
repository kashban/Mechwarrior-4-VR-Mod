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
        if ((Get-FileHash $From).Hash -eq (Get-FileHash $To).Hash) { return }
        $keep = "$To.before_mw4vr"
        if (-not (Test-Path $keep)) { Copy-Item $To $keep }
    }
    Copy-Item $From $To -Force
}

function Install-Mod([string]$Game, [bool]$Dinput) {
    $src = Join-Path $script:Pkg 'game'
    Copy-Kept (Join-Path $src 'd3d11.dll') (Join-Path $Game 'd3d11.dll')
    if ($Dinput) { Copy-Kept (Join-Path $src 'dinput.dll') (Join-Path $Game 'dinput.dll') }
    foreach ($f in 'mw4vr_cockpit_map.ini', 'mw4vr_start.cmd', 'mw4vr_start.ps1', 'mw4vr_readme.txt') {
        Copy-Kept (Join-Path $src $f) (Join-Path $Game $f)
    }
    # settings of the player's own are never replaced
    $ini = Join-Path $Game 'mw4vr.ini'
    if (Test-Path $ini) {
        Copy-Item (Join-Path $src 'mw4vr.ini') "$ini.new" -Force
        Say 'mw4vr.ini is there already: kept. The new default settings are beside it as mw4vr.ini.new.'
    } else {
        Copy-Item (Join-Path $src 'mw4vr.ini') $ini
    }
    $mechs = Join-Path $Game 'mw4vr_mechs.ini'
    if (-not (Test-Path $mechs)) { Copy-Item (Join-Path $src 'mw4vr_mechs.ini') $mechs }
    $cp = Join-Path $Game 'mw4vr_cockpits'
    New-Item -ItemType Directory $cp -Force | Out-Null
    $n = 0
    Get-ChildItem (Join-Path $src 'mw4vr_cockpits') -Filter *.ini -ErrorAction SilentlyContinue | ForEach-Object {
        $t = Join-Path $cp $_.Name
        if (-not (Test-Path $t)) { Copy-Item $_.FullName $t; $n++ }
    }
    Say "The mod is in the game's folder ($n cockpit settings new)."
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
    foreach ($c in @(@('python', ''), @('py', '-3'))) {
        $cmd = Get-Command $c[0] -ErrorAction SilentlyContinue
        if (-not $cmd -or $cmd.Source -like '*WindowsApps*') { continue }
        $a = @(); if ($c[1]) { $a += $c[1] }
        try {
            $v = & $cmd.Source @a -c 'import sys; print(sys.version_info[0])' 2>$null
            if ("$v".Trim() -ne '3') { continue }
            & $cmd.Source @a -c 'import numpy, PIL' 2>$null | Out-Null
            $Modules.Value = ($LASTEXITCODE -eq 0)
            return @{ Exe = $cmd.Source; Arg = $c[1] }
        } catch { }
    }
    return $null
}

if ($NoGui) { return }

# ---- the window ---------------------------------------------------------------------------------
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$f = New-Object Windows.Forms.Form
$f.Text = 'MW4VR Setup'
$f.ClientSize = New-Object Drawing.Size(720, 740)
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
foreach ($g in @((Split-Path -Parent $script:Pkg), (Split-Path -Parent (Split-Path -Parent $script:Pkg)))) { if ($g -and (Test-Game $g)) { $guess = $g } }
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
[void](Add-Label '...\MW5Mercs\Content\Paks of MechWarrior 5: Mercenaries:')
$tPaks, $bPaks = Add-PathRow (Find-Mw5Paks)
$lMw5 = Add-Label ''
$bEpic = New-Object Windows.Forms.Button
$bEpic.Text = 'MechWarrior 5 in the Epic Games Store'; $bEpic.Location = New-Object Drawing.Point(14, $script:y); $bEpic.Size = New-Object Drawing.Size(250, 25)
$bSteam = New-Object Windows.Forms.Button
$bSteam.Text = 'MechWarrior 5 on Steam'; $bSteam.Location = New-Object Drawing.Point(272, $script:y); $bSteam.Size = New-Object Drawing.Size(180, 25)
$f.Controls.AddRange(@($bEpic, $bSteam)); $script:y += 32
[void](Add-Label 'UE Viewer (umodel_64.exe) - a free tool by Gildor; download and unpack it, then choose the exe:')
$um = Join-Path $script:Pkg 'cockpit_tools\umodel\umodel_64.exe'
$tUm, $bUm = Add-PathRow $(if (Test-Path $um) { $um } else { '' })
$bGet = New-Object Windows.Forms.Button
$bGet.Text = 'Open the UE Viewer download page'; $bGet.Location = New-Object Drawing.Point(14, $script:y); $bGet.Size = New-Object Drawing.Size(230, 25)
$f.Controls.Add($bGet); $script:y += 30
$lPy = Add-Label ''
$cKeep = Add-Check 'Delete the exported files (2 GB) when the cockpits are converted' $true
$script:y += 6
$bGo = New-Object Windows.Forms.Button
$bGo.Text = 'Install'; $bGo.Location = New-Object Drawing.Point(14, $script:y); $bGo.Size = New-Object Drawing.Size(120, 30)
$bClose = New-Object Windows.Forms.Button
$bClose.Text = 'Close'; $bClose.Location = New-Object Drawing.Point(586, $script:y); $bClose.Size = New-Object Drawing.Size(120, 30)
$f.Controls.AddRange(@($bGo, $bClose)); $script:y += 38
$log = New-Object Windows.Forms.TextBox
$log.Multiline = $true; $log.ReadOnly = $true; $log.ScrollBars = 'Vertical'
$log.Location = New-Object Drawing.Point(14, $script:y); $log.Size = New-Object Drawing.Size(692, (740 - $script:y - 12))
$log.Font = New-Object Drawing.Font('Consolas', 8.5)
$f.Controls.Add($log)
$script:Say = { param($t) $log.AppendText($t + "`r`n") }

$script:Py = $null
$script:PyMods = $false
function Show-Game {
    $g = $tGame.Text.Trim()
    if (-not (Test-Game $g)) { $lGame.Text = 'MW4Mercs.exe is not in this folder.'; $lGame.ForeColor = 'Firebrick'; return }
    switch (Get-HookState $g) {
        'ok' { $lGame.Text = 'Found: the game and MW4Engine DX11 in the build MW4VR was made for.'; $lGame.ForeColor = 'DarkGreen' }
        'other' { $lGame.Text = 'Found the game. MW4Engine DX11 is another build than MW4VR was made for: parts of MW4VR will stay off.'; $lGame.ForeColor = 'DarkOrange' }
        default { $lGame.Text = 'Found the game, but no mw4hook.dll: install MW4Engine DX11 first.'; $lGame.ForeColor = 'Firebrick' }
    }
}
function Show-Python {
    $m = $false
    $script:Py = Find-Python ([ref]$m)
    $script:PyMods = $m
    if (-not $script:Py) { $lPy.Text = 'Python 3 was not found - needed for the cockpits: install it from python.org ("Add to PATH"), then start the setup again.'; $lPy.ForeColor = 'Firebrick' }
    elseif (-not $m) { $lPy.Text = 'Python 3 found; its packages numpy and Pillow are missing - the setup installs them (pip) when it converts.'; $lPy.ForeColor = 'DarkOrange' }
    else { $lPy.Text = 'Python 3 with numpy and Pillow found.'; $lPy.ForeColor = 'DarkGreen' }
}
# without MechWarrior 5: Mercenaries on this PC the cockpits cannot be chosen - the stores' pages are a click away
function Show-Mw5 {
    $p = $tPaks.Text.Trim()
    $have = [bool]($p -and (Test-Path $p) -and @(Get-ChildItem $p -Filter *.pak -ErrorAction SilentlyContinue).Count)
    $cMw5.Enabled = $have
    if (-not $have) { $cMw5.Checked = $false }
    if ($have) { $lMw5.Text = 'MechWarrior 5: Mercenaries found.'; $lMw5.ForeColor = 'DarkGreen' }
    else { $lMw5.Text = 'MechWarrior 5: Mercenaries was not found on this PC. Buy or install it, then choose its Paks folder or start the setup again:'; $lMw5.ForeColor = 'DarkOrange' }
}
$tPaks.Add_TextChanged({ Show-Mw5 })
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
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$(Join-Path $script:Pkg 'mw4vr_setup_cockpits.ps1')`"",
        '-Tools', "`"$(Join-Path $script:Pkg 'cockpit_tools')`"", '-Paks', "`"$($tPaks.Text.Trim())`"", '-Umodel', "`"$($tUm.Text.Trim())`"",
        '-Game', "`"$g`"", '-Python', "`"$($script:Py.Exe)`"", '-Log', "`"$($script:JobOut)`"")
    if ($script:Py.Arg) { $a += @('-PythonArg', $script:Py.Arg) }
    if (-not $script:PyMods) { $a += '-Pip' }
    if ($cKeep.Checked) { $a += '-DeleteExport' }
    $script:Job = Start-Process powershell -ArgumentList $a -WindowStyle Hidden -PassThru
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
