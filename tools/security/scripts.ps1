# The checkers for the scripts: bandit on the Python tools and tests, PSScriptAnalyzer on the PowerShell scripts.
#   powershell -File tools\security\scripts.ps1
# BANDIT: the path of bandit.exe (pip install bandit, best into a venv of its own).
# PSSA:   the path of PSScriptAnalyzer.psd1 (the module from the PowerShell Gallery, unpacked); without it the
#         module installed on the machine is used.
# The logs go to mod\build\analyze\bandit.log and pssa.log.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'mod\build\analyze'
New-Item -ItemType Directory $out -Force | Out-Null
Push-Location $root
try {
    if (-not $env:BANDIT) { throw 'set BANDIT to the path of bandit.exe' }
    # (bandit ends with 1 when it has found something: not an error of this script)
    $ErrorActionPreference = 'Continue'
    & $env:BANDIT -r tools tests -q -f txt -o (Join-Path $out 'bandit.log') 2>$null
    $ErrorActionPreference = 'Stop'
    $b = Get-Content (Join-Path $out 'bandit.log')
    "bandit: {0} findings ({1} medium, {2} high)" -f @($b -match '^>> Issue').Count, @($b -match 'Severity: Medium').Count, @($b -match 'Severity: High').Count

    if ($env:PSSA) { Import-Module $env:PSSA } else { Import-Module PSScriptAnalyzer }
    $r = @(Invoke-ScriptAnalyzer -Path tools -Recurse -Severity Error, Warning)
    $r | ForEach-Object { "{0}:{1}: [{2}/{3}] {4}" -f (Resolve-Path -Relative $_.ScriptPath), $_.Line, $_.Severity, $_.RuleName, $_.Message } |
        Set-Content (Join-Path $out 'pssa.log') -Encoding UTF8
    "PSScriptAnalyzer: {0} findings ({1} errors)" -f $r.Count, @($r | Where-Object Severity -eq 'Error').Count
} finally { Pop-Location }
