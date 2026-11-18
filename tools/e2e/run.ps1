# An end-to-end test of the mod in the game, without a headset on anybody's head:
#   powershell -File tools\e2e\run.ps1 -Game <the game's folder> -Pilot <pilot> [-Mech 'Mad Cat'] [-Cockpit madcat]
#       [-Runtime steamvr-null|active] [-Seconds 62] [-Yaw '7:0 10:45 14:45 17:0'] [-Shots '1 19.2 26 35 43 46'] [-KeepDll]
#       [-Keys '19:LMB:0.4 20.5:LMB:0.4 23:W:5 33:V:0.1 37:V:0.1 37.5:V:0.1 38:V:0.1 42:ESC:0.1 43.5:MOVE:0.5,0.459 44:LMB:0.15 48:MOVE:0.835,0.767 48.5:LMB:0.15 51:ESC:0.1 53.5:CTRL:0.6 53.7:F7:0.1 55:CTRL:0.6 55.2:F11:0.1 58:CTRL:0.6 58.2:F11:0.1']
#       [-Walk 23:31] [-Fire 19:22] [-Outside 33:38] [-Menu 42:44] [-Options 44:48.5] [-Off 55.2:58.2]      (what the keys are to do, for the judge; the
#       view key steps through four views, three of them outside;
#       'none' leaves a check out. The two clicks (MOVE, LMB) open the options' screen from the paused battle's menu and
#       close it; the pointer's places are measured for a window of 1300 x 1300 with its title bar. Ctrl+F7 at 53.7 s is a key for diagnosis that must do nothing: with it the eyes go off and the
#       run ends short. The keys are the game's own defaults - another pilot's keys need another -Keys.)
# What it does:
#   1. puts mod\build\d3d11.dll into the game's folder (the one that was there comes back at the end);
#   2. -Runtime steamvr-null: switches SteamVR to its "null" driver (no headset) and has the mod use SteamVR's
#      32-bit OpenXR for this start alone (XR_RUNTIME_JSON) - SteamVR's settings come back at the end;
#      -Runtime active: the machine's active OpenXR runtime as it is (the headset must be on and connected);
#   3. starts a battle through MW4Engine's "instant action by code" with the test plan (mod\src\e2e.inc),
#      waits until the battle has run -Seconds, ends the game;
#   4. judges the run (tools\e2e\judge.py) and keeps everything in mod\build\e2e\<time>.
# Exit code: 0 passed, 1 failed, 2 the run itself did not work.
# Not for the pipeline: it needs the game. Never while somebody plays - it ends every MW4Mercs.exe.
param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][string]$Pilot,
    [string]$Mech = 'Mad Cat',
    [string]$Cockpit = '',
    [ValidateSet('steamvr-null', 'active')][string]$Runtime = 'steamvr-null',
    [int]$Seconds = 62,
    [string]$Yaw = '7:0 10:45 14:45 17:0',
    [string]$Shots = '1 19.2 26 35 43 46',
    [string]$Keys = '19:LMB:0.4 20.5:LMB:0.4 23:W:5 33:V:0.1 37:V:0.1 37.5:V:0.1 38:V:0.1 42:ESC:0.1 43.5:MOVE:0.5,0.459 44:LMB:0.15 48:MOVE:0.835,0.767 48.5:LMB:0.15 51:ESC:0.1 53.5:CTRL:0.6 53.7:F7:0.1 55:CTRL:0.6 55.2:F11:0.1 58:CTRL:0.6 58.2:F11:0.1',
    [string]$Walk = '23:31',
    [string]$Fire = '19:22',
    [string]$Outside = '33:38',
    [string]$Menu = '42:44',
    [string]$Options = '44:48.5',
    [string]$Off = '55.2:58.2',
    [int]$TimeoutSec = 240,
    [switch]$KeepDll
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = Join-Path $Game 'MW4Mercs.exe'
$dll = Join-Path $Game 'd3d11.dll'
$new = Join-Path $root 'mod\build\d3d11.dll'
$csv = Join-Path $Game 'mw4vr_e2e.csv'
$log = Join-Path $Game 'mw4vr.log'
$out = Join-Path $root ('mod\build\e2e\' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
$vrProcs = 'vrserver', 'vrmonitor', 'vrcompositor', 'vrdashboard', 'vrwebhelper', 'vrstartup', 'steamtours'
function Say([string]$t) { Write-Host ("{0}  {1}" -f (Get-Date -Format 'HH:mm:ss'), $t) }
function Stop-All([string[]]$names, [int]$waitSec = 60) {
    for ($i = 0; $i -lt $waitSec * 2; $i++) {
        $left = @(Get-Process -Name $names -ErrorAction SilentlyContinue)
        if (-not $left.Count) { return $true }
        foreach ($p in $left) { try { $p.Kill() } catch { Write-Verbose "passed over: $($_.Exception.Message)" } }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

if (-not (Test-Path $exe)) { Say "no MW4Mercs.exe in $Game"; exit 2 }
if (-not (Test-Path $new)) { Say 'no mod\build\d3d11.dll - build the mod first (mod\build.bat)'; exit 2 }
if (Get-Process MW4Mercs -ErrorAction SilentlyContinue) { Say 'the game is running - nothing done'; exit 2 }
New-Item -ItemType Directory $out -Force | Out-Null

$settings = $null; $settingsKept = $null; $vrWasRunning = $false
$dllKept = Join-Path $out 'd3d11_before.dll'
$code = 2
try {
    if ($Runtime -eq 'steamvr-null') {
        $vrWasRunning = [bool]@(Get-Process -Name $vrProcs -ErrorAction SilentlyContinue).Count
        if ($vrWasRunning) { Say 'SteamVR is running - close it first (its settings are changed for the run)'; exit 2 }
        # SteamVR's folder: beside its 64-bit manifest among the machine's OpenXR runtimes
        $avail = (Get-Item 'HKLM:\SOFTWARE\Khronos\OpenXR\1\AvailableRuntimes' -ErrorAction SilentlyContinue).Property
        $manifest = @($avail | Where-Object { $_ -match 'steamxr_win64\.json$' } | ForEach-Object { Join-Path (Split-Path -Parent $_) 'steamxr_win32.json' } |
            Where-Object { Test-Path $_ })[0]
        if (-not $manifest) { Say 'SteamVR (its 32-bit OpenXR manifest steamxr_win32.json) was not found'; exit 2 }
        $steam = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction Stop).SteamPath
        $settings = Join-Path $steam 'config\steamvr.vrsettings'
        if (-not (Test-Path $settings)) { Say "SteamVR's settings were not found: $settings"; exit 2 }
        $settingsKept = Join-Path $out 'steamvr.vrsettings.before'
        Copy-Item $settings $settingsKept
        & python (Join-Path $PSScriptRoot 'steamvr_null.py') $settings
        if ($LASTEXITCODE -ne 0) { throw "SteamVR's settings could not be changed" }
        $env:XR_RUNTIME_JSON = $manifest
        Say "SteamVR without a headset (null driver): $manifest"
    }

    # the pilot's files and the game's options: a battle started by code may change them
    Copy-Item (Join-Path $Game 'Resource\Pilots') (Join-Path $out 'Pilots') -Recurse
    if (Test-Path (Join-Path $Game 'options.ini')) { Copy-Item (Join-Path $Game 'options.ini') $out }
    Copy-Item $dll $dllKept
    Copy-Item $new $dll -Force
    Remove-Item $csv -ErrorAction SilentlyContinue
    $plan = Join-Path $out 'plan.ini'
    Set-Content $plan -Value @('[e2e]', "yaw=$Yaw", "shots=$Shots", "keys=$Keys") -Encoding ASCII
    $shotsDir = Join-Path $Game 'mw4vr_shots'
    $shotsBefore = @(Get-ChildItem $shotsDir -Filter *.bmp -ErrorAction SilentlyContinue | ForEach-Object Name)

    $env:MW4VR_E2E = $plan
    $env:MW4HOOK_IA = "pilot=$Pilot;mech=$Mech"
    $env:__COMPAT_LAYER = 'RunAsInvoker'
    Say "starting the game: $Mech, plan '$Yaw', $Seconds s of battle"
    $started = Get-Date
    $p = Start-Process -FilePath $exe -WorkingDirectory $Game -PassThru
    $why = "no end after $TimeoutSec s"
    while ($true) {
        Start-Sleep -Seconds 1
        $p.Refresh()
        if ($p.HasExited) { $why = "the game ended by itself (code $('{0:x8}' -f $p.ExitCode))"; break }
        $t = -1.0
        if (Test-Path $csv) {
            # the newest whole line's third field: seconds since the camera sat down
            try {
                $fs = [IO.File]::Open($csv, 'Open', 'Read', 'ReadWrite')
                try {
                    [void]$fs.Seek([Math]::Max(0, $fs.Length - 600), 'Begin')
                    $tail = (New-Object IO.StreamReader($fs)).ReadToEnd() -split "`n"
                } finally { $fs.Close() }
                if ($tail.Count -ge 3) { $t = [double]::Parse(($tail[$tail.Count - 2] -split ',')[2], [Globalization.CultureInfo]::InvariantCulture) }
            } catch { Write-Verbose "passed over: $($_.Exception.Message)" }
        }
        if ($t -ge $Seconds) { $why = 'done'; break }
        if (((Get-Date) - $started).TotalSeconds -ge $TimeoutSec) { break }
    }
    Say "the run: $why after $([int]((Get-Date) - $started).TotalSeconds) s"
    [void](Stop-All @('MW4Mercs') 90)

    foreach ($f in $csv, $log, (Join-Path $Game 'mw4hook.log')) { if (Test-Path $f) { Copy-Item $f $out } }
    Get-ChildItem $shotsDir -Filter *.bmp -ErrorAction SilentlyContinue | Where-Object { $shotsBefore -notcontains $_.Name } | Move-Item -Destination $out
    if (-not (Test-Path (Join-Path $out 'mw4vr_e2e.csv'))) {
        Say 'the mod wrote no mw4vr_e2e.csv - see mw4vr.log in the run''s folder'
        $code = 2
    } else {
        $a = @((Join-Path $PSScriptRoot 'judge.py'), (Join-Path $out 'mw4vr_e2e.csv'), (Join-Path $out 'mw4vr.log'), '--seconds', ($Seconds - 2))
        if ($Cockpit) { $a += @('--cockpit', $Cockpit) }
        if ($Keys) {
            if ($Walk -and $Walk -ne 'none') { $a += @('--walk', $Walk) }
            if ($Fire -and $Fire -ne 'none') { $a += @('--fire', $Fire) }
            if ($Outside -and $Outside -ne 'none') { $a += @('--outside', $Outside) }
            if ($Menu -and $Menu -ne 'none') { $a += @('--menu', $Menu) }
            if ($Off -and $Off -ne 'none') { $a += @('--off', $Off) }
            if ($Options -and $Options -ne 'none') { $a += @('--options', $Options) }
        }
        & python @a | Tee-Object -FilePath (Join-Path $out 'verdict.txt')
        $code = $LASTEXITCODE
    }
} finally {
    $env:MW4VR_E2E = $null; $env:MW4HOOK_IA = $null; $env:XR_RUNTIME_JSON = $null
    $gone = Stop-All @('MW4Mercs') 90
    if ($Runtime -eq 'steamvr-null' -and -not $vrWasRunning -and $settingsKept) {
        [void](Stop-All $vrProcs 30) # (SteamVR writes its settings when it ends: only then the old ones go back)
        if (Test-Path $settingsKept) { Copy-Item $settingsKept $settings -Force; Say "SteamVR's settings are as they were." }
    }
    if ($gone -and (Test-Path (Join-Path $out 'Pilots'))) {
        Copy-Item (Join-Path $out 'Pilots\*') (Join-Path $Game 'Resource\Pilots') -Recurse -Force
        if (Test-Path (Join-Path $out 'options.ini')) { Copy-Item (Join-Path $out 'options.ini') (Join-Path $Game 'options.ini') -Force }
    }
    if ((Test-Path $dllKept) -and -not $KeepDll) {
        if ($gone) { Copy-Item $dllKept $dll -Force; Say "the game's d3d11.dll is the one from before again." }
        else { Say "THE GAME DID NOT END: d3d11.dll was NOT put back - it is kept as $dllKept" }
    }
    Say "everything of this run: $out"
}
exit $code
