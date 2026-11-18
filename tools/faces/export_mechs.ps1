# Starts the game once per mech with mw4hook's "instant action by code" and model_dump=1, waits until the
# battle draws, ends the game. The pilot's files and mw4hook.ini are put back at the end.
#   export_mechs.ps1 -Game <the game's folder> -Pilot <pilot name> -Mechs 'Argus','Mad Cat',...
# The models land in <game>\model_dump, the textures in <game>\texture_dump (mw4faces.py draws them).
[Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSReviewUnusedParameter', 'Out', Justification = 'used by the function Say')]
param([Parameter(Mandatory)][string]$Game, [Parameter(Mandatory)][string]$Pilot, [string[]]$Mechs, [string]$Extra = '', [int]$Wait = 100,
      [string]$Out = "$PSScriptRoot\export_mechs.log")
$dir = $Game
$exe = Join-Path $dir 'MW4Mercs.exe'
$hk = Join-Path $dir 'mw4hook.log'
$ini = Join-Path $dir 'mw4hook.ini'
$keep = Join-Path $PSScriptRoot 'dumprun_keep'
$env:__COMPAT_LAYER = 'RunAsInvoker'
function Say($t) { Add-Content -Path $Out -Value ("{0}  {1}" -f (Get-Date -Format 'HH:mm:ss'), $t) -Encoding utf8 }
function EndGame {
    for ($i = 0; $i -lt 120; $i++) {
        $left = @(Get-Process -Name MW4Mercs -ErrorAction SilentlyContinue)
        if ($left.Count -eq 0) { return }
        foreach ($q in $left) { try { $q.Kill() } catch { Write-Verbose "passed over: $($_.Exception.Message)" } }
        Start-Sleep -Milliseconds 500
    }
}
if (Get-Process -Name MW4Mercs -ErrorAction SilentlyContinue) { Say 'the game is running - nothing done'; exit 1 }
if (-not (Test-Path $keep)) {
    New-Item -ItemType Directory $keep | Out-Null
    Copy-Item $ini (Join-Path $keep 'mw4hook.ini')
    Copy-Item (Join-Path $dir 'Resource\Pilots') (Join-Path $keep 'Pilots') -Recurse
    Copy-Item (Join-Path $dir 'options.ini') (Join-Path $keep 'options.ini')
}
(Get-Content $ini) -replace '^(model_dump|texture_dump)=0', '$1=1' | Set-Content $ini -Encoding ascii
try {
    foreach ($m in $Mechs) {
        EndGame
        $from = (Get-Content $hk).Count
        $env:MW4HOOK_IA = "pilot=$Pilot;mech=$m$Extra"
        $started = Get-Date
        $p = Start-Process -FilePath $exe -WorkingDirectory $dir -PassThru
        $res = 'timeout'
        $seen = $null
        $battle = $null
        $count = 0
        $md = Join-Path $dir 'model_dump\mechs'
        $before = @(Get-ChildItem $md -Directory -ErrorAction SilentlyContinue | ForEach-Object Name)
        while ($true) {
            Start-Sleep -Milliseconds 1000
            $age = ((Get-Date) - $started).TotalSeconds
            $p.Refresh()
            if ($p.HasExited) { $res = "ended $('{0:x8}' -f $p.ExitCode) after $([int]$age) s"; break }
            $now = @(Get-ChildItem $md -Directory -ErrorAction SilentlyContinue | ForEach-Object Name | Where-Object { $before -notcontains $_ })
            $files = 0
            foreach ($d in $now) { $files += @(Get-ChildItem (Join-Path $md $d) -File).Count }
            if ($files -ne $count) { $count = $files; $seen = Get-Date }
            if ($count -gt 0 -and ((Get-Date) - $seen).TotalSeconds -ge 5) { $res = "ok after $([int]$age) s: $($now -join ',') $count files"; break }
            $lines = @(Get-Content $hk | Select-Object -Skip $from | Where-Object { $_ -match 'lighting: map' })
            if ($lines.Count -and -not $battle) { $battle = Get-Date }
            if ($battle -and $count -eq 0 -and ((Get-Date) - $battle).TotalSeconds -ge 10) { $res = "battle, nothing new after $([int]$age) s"; break }
            if ($age -ge $Wait) { break }
        }
        $new = @(Get-Content $hk | Select-Object -Skip $from)
        $ia = @($new | Where-Object { $_ -match 'instant action by code: (launching|NOT|no |.*no mech)' } | ForEach-Object { $_.Substring(21, [Math]::Min(260, $_.Length - 21)) })
        Say "$m : $res | $($ia -join ' | ')"
    }
} finally {
    EndGame
    $env:MW4HOOK_IA = $null
    Copy-Item (Join-Path $keep 'mw4hook.ini') $ini -Force
    Copy-Item (Join-Path $keep 'options.ini') (Join-Path $dir 'options.ini') -Force
    Copy-Item (Join-Path $keep 'Pilots\*') (Join-Path $dir 'Resource\Pilots') -Recurse -Force
    Say 'done'
}
