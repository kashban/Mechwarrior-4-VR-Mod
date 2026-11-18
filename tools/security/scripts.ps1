# The checkers for the scripts: bandit on the Python tools and tests, PSScriptAnalyzer on the PowerShell scripts.
#   powershell -File tools\security\scripts.ps1
# BANDIT: the path of bandit.exe (pip install bandit, best into a venv of its own); without it "bandit" as the
#         shell finds it (the pipeline: installed from requirements-ci.txt).
# PSSA:   the path of PSScriptAnalyzer.psd1 (the module from the PowerShell Gallery, unpacked); without it the
#         module installed on the machine is used.
# The logs go to mod\build\analyze\bandit.log and pssa.log.
# Exit code: 1 when bandit names a finding of medium or high severity or PSScriptAnalyzer anything at all (the
# pipeline's gate; its rules: PSScriptAnalyzerSettings.psd1); bandit's low ones are listed in the log.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$out = Join-Path $root 'mod\build\analyze'
New-Item -ItemType Directory $out -Force | Out-Null
Push-Location $root
try {
    $bandit = if ($env:BANDIT) { $env:BANDIT } else { 'bandit' }
    if (-not (Get-Command $bandit -ErrorAction SilentlyContinue)) { throw 'no bandit: set BANDIT to the path of bandit.exe' }
    # (bandit ends with 1 when it has found something: not an error of this script)
    $ErrorActionPreference = 'Continue'
    & $bandit -r tools tests -q -f txt -o (Join-Path $out 'bandit.log') 2>$null
    $ErrorActionPreference = 'Stop'
    $b = Get-Content (Join-Path $out 'bandit.log')
    "bandit: {0} findings ({1} medium, {2} high)" -f @($b -match '^>> Issue').Count, @($b -match 'Severity: Medium').Count, @($b -match 'Severity: High').Count

    if ($env:PSSA) { Import-Module $env:PSSA } else { Import-Module PSScriptAnalyzer }
    $r = @(Invoke-ScriptAnalyzer -Path tools -Recurse -Severity Error, Warning -Settings (Join-Path $root 'PSScriptAnalyzerSettings.psd1'))
    # (also when there is none: an old run's log must not stay)
    Set-Content (Join-Path $out 'pssa.log') -Encoding UTF8 -Value @($r | ForEach-Object {
            "{0}:{1}: [{2}/{3}] {4}" -f (Resolve-Path -Relative $_.ScriptPath), $_.Line, $_.Severity, $_.RuleName, $_.Message })
    $errors = $r.Count
    "PSScriptAnalyzer: {0} findings" -f $r.Count
    $grave = @($b -match 'Severity: (Medium|High)').Count
    if ($grave -or $errors) {
        $b -match '^>> Issue|Severity: (Medium|High)|Location:' | Select-Object -First 30
        Get-Content (Join-Path $out 'pssa.log') | Select-Object -First 30
        exit 1
    }
} finally { Pop-Location }
exit 0 # (not bandit's own code: it ends with 1 for its low findings too)
