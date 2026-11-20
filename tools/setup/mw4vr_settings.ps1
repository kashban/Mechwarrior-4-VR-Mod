# MW4VR settings: a window for mw4vr.ini - every key with what its comment in the file says, a tick box, a list or
# a field for its value. Started by mw4vr_settings.cmd from the game's folder, or by the setup's button.
# The window is made from the file itself: a key a later version adds shows up without a change here.
#   -Ini <file>: another mw4vr.ini than the one beside this script
#   -NoGui: only the functions (for tests):  . .\mw4vr_settings.ps1 -NoGui
param([string]$Ini = '', [switch]$NoGui)
$ErrorActionPreference = 'Stop'
if (-not $Ini) { $Ini = Join-Path $PSScriptRoot 'mw4vr.ini' }

# keys with a few values to choose from: value -> what it means
$script:Choices = @{
    'replay_right' = [ordered]@{
        '2' = '2 - automatic: whichever is faster (recommended)'
        '1' = '1 - the mod repeats the left eye''s draws for the right eye'
        '0' = '0 - the game draws both eyes itself'
    }
}
# keys whose value 0 or 1 is a number, not a switch
$script:Numbers = @('mw5_paint', 'hud_panel_x_m', 'hud_panel_y_m', 'seat_up_m', 'seat_forward_m')

# The file's keys in their order: Section, Group (the "; --- heading ---" above them), Key, Value and Note (the
# comment lines above the key; keys that follow one another without a comment share the first one's).
function Read-SettingList([string]$Path) {
    $list = New-Object Collections.Generic.List[object]
    $sec = ''; $group = 'General'; $note = @(); $last = ''
    foreach ($l in [IO.File]::ReadAllLines($Path, [Text.Encoding]::Default)) {
        if ($l -match '^\s*\[(.+?)\]') {
            $sec = $Matches[1].ToLower(); $note = @(); $last = ''
            if ($sec -ne 'vr') { $group = $sec.Substring(0, 1).ToUpper() + $sec.Substring(1) }
            continue
        }
        if ($l -match '^\s*;\s*-{2,}\s*(.+?)\s*-{2,}\s*$') { $group = $Matches[1]; $note = @(); $last = ''; continue }
        if ($l -match '^\s*;\s?(.*)$') { $note += $Matches[1]; continue }
        if ($l -match '^\s*([^;=\s][^=]*?)\s*=\s*(.*?)\s*$') {
            if (-not $sec) { continue } # (a key before any section is nobody's)
            if ($note.Count) { $last = ($note -join ' ') }
            $list.Add([pscustomobject]@{ Section = $sec; Group = $group; Key = $Matches[1]; Value = $Matches[2]; Note = $last })
            $note = @()
            continue
        }
        if (-not $l.Trim()) { $note = @(); $last = '' }
    }
    return , $list
}

# 'choice', 'check' or 'text': how a key's value is shown
function Get-SettingKind($Entry) {
    if ($script:Choices.ContainsKey($Entry.Key)) { return 'choice' }
    if ($Entry.Value -in '0', '1' -and $script:Numbers -notcontains $Entry.Key) { return 'check' }
    return 'text'
}

# a short title for a tab from a heading ("sizes and the seat (the values ...)" -> "Sizes and the seat")
function Get-TabTitle([string]$Group) {
    $t = ($Group -split '\s*[(:]')[0].Trim()
    $t = $t -replace '^(the|a)\s+', ''
    if ($t.Length -gt 30) { $t = $t.Substring(0, 30).TrimEnd() + '...' }
    if (-not $t) { return 'Settings' }
    return $t.Substring(0, 1).ToUpper() + $t.Substring(1)
}

# Writes values into the file: $Changes is "section|key" -> value. Only those lines change; comments, order and
# every other key stay. The file as it was the first time: <file>.before_settings. -> how many lines changed
function Save-SettingList([string]$Path, [hashtable]$Changes) {
    $enc = [Text.Encoding]::Default
    $lines = [IO.File]::ReadAllLines($Path, $enc)
    $sec = ''; $n = 0
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*\[(.+?)\]') { $sec = $Matches[1].ToLower(); continue }
        if ($lines[$i] -match '^\s*([^;=\s][^=]*?)\s*=\s*(.*?)\s*$') {
            $k = "$sec|$($Matches[1].ToLower())"
            if ($Changes.ContainsKey($k) -and "$($Changes[$k])" -ne $Matches[2]) {
                $lines[$i] = "$($Matches[1])=$($Changes[$k])"
                $n++
            }
        }
    }
    if ($n) {
        if (-not (Test-Path "$Path.before_settings")) { Copy-Item $Path "$Path.before_settings" }
        [IO.File]::WriteAllLines($Path, $lines, $enc)
    }
    return $n
}

# a value typed into a field: no line break, no leading or trailing blanks ('' stays '')
function Format-SettingValue([string]$Text) { return (($Text -replace '[\r\n]+', ' ').Trim()) }

if ($NoGui) { return }

# ---- the window ---------------------------------------------------------------------------------
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
if (-not (Test-Path $Ini)) {
    [void][Windows.Forms.MessageBox]::Show("mw4vr.ini was not found:`r`n$Ini`r`n`r`nInstall MW4VR with its setup first.", 'MW4VR Settings')
    return
}
$defaultsFile = "$Ini.default" # (the setup puts the package's file there)
$script:Entries = Read-SettingList $Ini
$script:Defaults = @{}
if (Test-Path $defaultsFile) { foreach ($e in (Read-SettingList $defaultsFile)) { $script:Defaults["$($e.Section)|$($e.Key.ToLower())"] = $e.Value } }

$f = New-Object Windows.Forms.Form
$f.Text = 'MW4VR Settings'
$f.ClientSize = New-Object Drawing.Size(900, 720)
$f.StartPosition = 'CenterScreen'
$f.MinimumSize = New-Object Drawing.Size(760, 480)
$f.Font = New-Object Drawing.Font('Segoe UI', 9)
$bold = New-Object Drawing.Font('Segoe UI', 9, [Drawing.FontStyle]::Bold)
$top = New-Object Windows.Forms.Label
$top.Text = "$Ini`r`nMost settings work within two seconds while the game runs; where a text says ""read at the start"", at the next start."
$top.Location = New-Object Drawing.Point(12, 8); $top.Size = New-Object Drawing.Size(876, 36); $top.Anchor = 'Top, Left, Right'
$tabs = New-Object Windows.Forms.TabControl
$tabs.Location = New-Object Drawing.Point(12, 48); $tabs.Size = New-Object Drawing.Size(876, 620); $tabs.Anchor = 'Top, Bottom, Left, Right'
$bSave = New-Object Windows.Forms.Button
$bSave.Text = 'Save'; $bSave.Location = New-Object Drawing.Point(12, 678); $bSave.Size = New-Object Drawing.Size(110, 30); $bSave.Anchor = 'Bottom, Left'
$bDef = New-Object Windows.Forms.Button
$bDef.Text = 'Defaults (this page)'; $bDef.Location = New-Object Drawing.Point(130, 678); $bDef.Size = New-Object Drawing.Size(150, 30); $bDef.Anchor = 'Bottom, Left'
$bDef.Enabled = [bool]$script:Defaults.Count
$status = New-Object Windows.Forms.Label
$status.Location = New-Object Drawing.Point(292, 685); $status.Size = New-Object Drawing.Size(470, 20); $status.Anchor = 'Bottom, Left, Right'
$bClose = New-Object Windows.Forms.Button
$bClose.Text = 'Close'; $bClose.Location = New-Object Drawing.Point(778, 678); $bClose.Size = New-Object Drawing.Size(110, 30); $bClose.Anchor = 'Bottom, Right'
$f.Controls.AddRange(@($top, $tabs, $bSave, $bDef, $status, $bClose))
$tip = New-Object Windows.Forms.ToolTip
$tip.AutoPopDelay = 30000

# one row for each key: its name, the control for its value, the comment's text
$script:Rows = New-Object Collections.Generic.List[object]
$pages = [ordered]@{}
$noteW = 500
foreach ($e in $script:Entries) {
    if (-not $pages.Contains($e.Group)) {
        $tp = New-Object Windows.Forms.TabPage
        $tp.Text = Get-TabTitle $e.Group
        $tp.AutoScroll = $true
        $tp.Padding = New-Object Windows.Forms.Padding(6)
        $tabs.TabPages.Add($tp)
        $pages[$e.Group] = @{ Page = $tp; Y = 10; Last = '' }
    }
    $pg = $pages[$e.Group]
    $name = New-Object Windows.Forms.Label
    $name.Text = $e.Key; $name.Font = $bold
    $name.Location = New-Object Drawing.Point(10, ($pg.Y + 3)); $name.Size = New-Object Drawing.Size(170, 18)
    $kind = Get-SettingKind $e
    if ($kind -eq 'check') {
        $c = New-Object Windows.Forms.CheckBox
        $c.Checked = ($e.Value -eq '1'); $c.Text = 'on'
        $c.Size = New-Object Drawing.Size(150, 22)
    } elseif ($kind -eq 'choice') {
        $c = New-Object Windows.Forms.ComboBox
        $c.DropDownStyle = 'DropDownList'
        $c.Size = New-Object Drawing.Size(150, 23); $c.DropDownWidth = 420
        $ch = $script:Choices[$e.Key]
        foreach ($v in $ch.Keys) { [void]$c.Items.Add($ch[$v]) }
        if (-not $ch.Contains($e.Value)) { [void]$c.Items.Add("$($e.Value) - (as in the file)") }
        $c.SelectedIndex = [Math]::Max(0, [array]::IndexOf(@($c.Items | ForEach-Object { ("$_" -split ' ')[0] }), $e.Value))
    } else {
        $c = New-Object Windows.Forms.TextBox
        $c.Text = $e.Value
        $c.Size = New-Object Drawing.Size(150, 23)
    }
    $c.Location = New-Object Drawing.Point(186, $pg.Y)
    $note = New-Object Windows.Forms.Label
    # (keys that share one comment: its text at the first of them only)
    $note.Text = if ($e.Note -and $e.Note -eq $pg.Last) { '(see the setting above)' } else { $e.Note }
    $pg.Last = $e.Note
    $note.MaximumSize = New-Object Drawing.Size($noteW, 0); $note.AutoSize = $true
    $note.Location = New-Object Drawing.Point(348, ($pg.Y + 3))
    $note.ForeColor = [Drawing.SystemColors]::GrayText
    $pg.Page.Controls.AddRange(@($name, $c, $note))
    if ($e.Note) { $tip.SetToolTip($name, $e.Note); $tip.SetToolTip($c, $e.Note) }
    $h = [Math]::Max(28, $note.PreferredHeight + 10)
    $pg.Y += $h
    $script:Rows.Add([pscustomobject]@{ Entry = $e; Kind = $kind; Control = $c; Page = $pg.Page })
}

function Get-RowValue($Row) {
    if ($Row.Kind -eq 'check') { if ($Row.Control.Checked) { return '1' } else { return '0' } }
    if ($Row.Kind -eq 'choice') { return ("$($Row.Control.SelectedItem)" -split ' ')[0] }
    return (Format-SettingValue $Row.Control.Text)
}
function Set-RowValue($Row, [string]$Value) {
    if ($Row.Kind -eq 'check') { $Row.Control.Checked = ($Value -eq '1'); return }
    if ($Row.Kind -eq 'choice') {
        for ($i = 0; $i -lt $Row.Control.Items.Count; $i++) { if (("$($Row.Control.Items[$i])" -split ' ')[0] -eq $Value) { $Row.Control.SelectedIndex = $i } }
        return
    }
    $Row.Control.Text = $Value
}
function Get-ChangeList {
    $ch = @{}
    foreach ($r in $script:Rows) {
        $v = Get-RowValue $r
        if ($v -ne $r.Entry.Value) { $ch["$($r.Entry.Section)|$($r.Entry.Key.ToLower())"] = $v }
    }
    return $ch
}
$bSave.Add_Click({
    $ch = Get-ChangeList
    if (-not $ch.Count) { $status.Text = 'Nothing has changed.'; return }
    try {
        $n = Save-SettingList $Ini $ch
        foreach ($r in $script:Rows) { $r.Entry.Value = Get-RowValue $r }
        $status.Text = "Saved: $n setting(s) changed. (The file as it first was: mw4vr.ini.before_settings)"
    } catch { $status.Text = "NOT saved: $($_.Exception.Message)" }
})
$bDef.Add_Click({
    $n = 0
    foreach ($r in $script:Rows) {
        if ($r.Page -ne $tabs.SelectedTab) { continue }
        $k = "$($r.Entry.Section)|$($r.Entry.Key.ToLower())"
        if ($script:Defaults.ContainsKey($k) -and (Get-RowValue $r) -ne $script:Defaults[$k]) { Set-RowValue $r $script:Defaults[$k]; $n++ }
    }
    $status.Text = if ($n) { "$n setting(s) of this page set to the defaults - not saved yet." } else { 'This page has the defaults already.' }
})
$bClose.Add_Click({ $f.Close() })
$f.Add_FormClosing({
    param($s, $ev)
    [void]$s # (the form itself: not needed)
    if ($env:MW4VR_SETTINGS_TEST) { return }
    if ((Get-ChangeList).Count) {
        $a = [Windows.Forms.MessageBox]::Show($f, 'Some settings were changed and not saved. Save them now?', 'MW4VR Settings', 'YesNoCancel')
        if ($a -eq 'Cancel') { $ev.Cancel = $true }
        elseif ($a -eq 'Yes') { $bSave.PerformClick() }
    }
})

if ($env:MW4VR_SETTINGS_TEST) { # (a test: the window is built and shown, then closed again; a .png: its picture)
    $f.Add_Shown({
        Write-Host "settings window ok: $($script:Rows.Count) settings on $($tabs.TabPages.Count) pages, defaults: $($script:Defaults.Count)"
        if ($env:MW4VR_SETTINGS_TEST -like '*.png') {
            if ($env:MW4VR_SETTINGS_TEST_PAGE) { $tabs.SelectedIndex = [int]$env:MW4VR_SETTINGS_TEST_PAGE; [Windows.Forms.Application]::DoEvents() }
            $bmp = New-Object Drawing.Bitmap($f.Width, $f.Height)
            $f.DrawToBitmap($bmp, (New-Object Drawing.Rectangle(0, 0, $f.Width, $f.Height)))
            $bmp.Save($env:MW4VR_SETTINGS_TEST)
        }
        $f.Close()
    })
}
[void]$f.ShowDialog()
