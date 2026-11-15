# MW4VR starter: MW4Mercs.exe often dies right at start-up (heap corruption while DirectInput
# enumerates the input devices, before any rendering) or hangs there with a grey or black window.
# (The mod's dinput.dll in the game folder removes that cause; this script stays as the safety net.)
# This starts it again until frames are really being presented: the probe's log (mw4vr.log,
# written anew by every start that gets as far as Direct3D) gains a statistics line "<n> fps | ..."
# with more than 5 fps, which it only writes while the game presents frames.
# An attempt is ended and repeated when the game ends, when one of its windows is hung for 6 s,
# when the probe has not been loaded after -LoadSec seconds, or when no frames came after -TimeoutSec.
# What happened is written to mw4vr_start.log.
param([int]$MaxTries = 10, [int]$TimeoutSec = 45, [int]$LoadSec = 20, [switch]$Admin)

$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$exe = Join-Path $dir 'MW4Mercs.exe'
$probeLog = Join-Path $dir 'mw4vr.log'
$startLog = Join-Path $dir 'mw4vr_start.log'

# MW4Mercs.exe asks for administrator rights in its manifest, so Windows asks the user at every start (and
# at every repeated attempt). The game does not need them when its folder is writable: start it as the
# user who called us. -Admin keeps the old behaviour.
if (-not $Admin) { $env:__COMPAT_LAYER = 'RunAsInvoker' }

Add-Type -Namespace MW4VR -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool IsHungAppWindow(IntPtr hWnd);
[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
public delegate bool EnumProc(IntPtr hWnd, IntPtr l);
public static bool AnyHung(uint pid) {
    bool hung = false;
    EnumWindows((h, l) => {
        uint p; GetWindowThreadProcessId(h, out p);
        if (p == pid && IsWindowVisible(h) && IsHungAppWindow(h)) { hung = true; return false; }
        return true;
    }, IntPtr.Zero);
    return hung;
}
'@

function Say($text) {
    Write-Host $text
    Add-Content -Path $startLog -Value ("{0}  {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $text) -Encoding utf8
}

function ReadShared($path) {
    if (-not (Test-Path $path)) { return '' }
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
        try { return (New-Object IO.StreamReader($fs)).ReadToEnd() } finally { $fs.Close() }
    } catch { return '' }
}

# Ends every MW4Mercs process and waits until they are really gone. A hung game can take a long
# time to die after it was told to (its threads are stuck in a driver); while it is still there a
# new start ends at once (the game allows one instance), and further kills answer "access denied".
function EndGame([int]$WaitSec = 90) {
    $since = Get-Date
    $told = $false
    while ($true) {
        $left = @(Get-Process -Name MW4Mercs -ErrorAction SilentlyContinue)
        if ($left.Count -eq 0) {
            if ($told) { Say "Old game process gone after $([int]((Get-Date) - $since).TotalSeconds) s." }
            return $true
        }
        foreach ($q in $left) { try { $q.Kill() } catch { } }
        if (-not $told) { Say "Waiting for the old game process ($($left[0].Id)) to end ..."; $told = $true }
        if (((Get-Date) - $since).TotalSeconds -ge $WaitSec) {
            Say "The old game process ($($left[0].Id)) does not end - close it in the Task Manager (or sign out), then start again."
            return $false
        }
        Start-Sleep -Milliseconds 500
    }
}

for ($try = 1; $try -le $MaxTries; $try++) {
    if (-not (EndGame)) { exit 2 }
    Say "Start $try of $MaxTries ..."
    $started = Get-Date
    $p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
    $hungSince = $null
    $why = "no frames after $TimeoutSec s"
    $ok = $false
    while ($true) {
        Start-Sleep -Milliseconds 500
        $age = ((Get-Date) - $started).TotalSeconds
        $p.Refresh()
        if ($p.HasExited) { $why = "ended (exit code $($p.ExitCode))"; break }
        # the probe's log of this start (the old one does not count)
        $fresh = (Test-Path $probeLog) -and (Get-Item $probeLog).LastWriteTime -ge $started.AddSeconds(-1)
        if ($fresh) {
            $m = [regex]::Matches((ReadShared $probeLog), '(?m)^\S+\s+([0-9.]+) fps \|')
            if ($m.Count -gt 0 -and [double]::Parse($m[$m.Count - 1].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture) -gt 5) {
                $ok = $true
                break
            }
        } elseif ($age -ge $LoadSec) {
            $why = "Direct3D not reached after $LoadSec s"
            break
        }
        # a hung window ("not responding") for 6 s
        if ([MW4VR.Win]::AnyHung([uint32]$p.Id) -or -not $p.Responding) {
            if (-not $hungSince) { $hungSince = Get-Date }
            elseif (((Get-Date) - $hungSince).TotalSeconds -ge 6) { $why = 'window not responding'; break }
        } else {
            $hungSince = $null
        }
        if ($age -ge $TimeoutSec) { break }
    }
    if ($ok) {
        Say "Running (attempt $try, frames after $([int]((Get-Date) - $started).TotalSeconds) s)."
        exit 0
    }
    if (-not $p.HasExited) {
        Say "Attempt ${try}: $why - ending it."
        if (-not (EndGame)) { exit 2 }
    } else {
        Say "Attempt ${try}: $why."
    }
    Start-Sleep -Seconds 1
}
Say "Gave up after $MaxTries attempts."
exit 1
