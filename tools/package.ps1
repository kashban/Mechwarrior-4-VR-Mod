# Builds the mod, runs the tests and puts together what a player needs:  release\MW4VR-v<version>.zip
#   powershell -File tools\package.ps1 [-Trees <folder with build_trees.py's output>] [-SkipTests]
# The version is the one the mod logs (mod\src\main.cpp). Nothing of MechWarrior 5 may be in the package:
# it is checked for cockpit and export files before the archive is made.
param([string]$Trees = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$m = [regex]::Match((Get-Content (Join-Path $root 'mod\src\main.cpp') -Raw), 'MW4VR v([0-9.]+) \(debug')
if (-not $m.Success) { throw 'no version found in mod\src\main.cpp' }
$name = "MW4VR-v$($m.Groups[1].Value)"

Push-Location (Join-Path $root 'mod')
try {
    cmd /c '.\build.bat > build\package_build.txt 2>&1'
    if ($LASTEXITCODE -ne 0) { throw 'the build failed - see mod\build\package_build.txt' }
} finally { Pop-Location }
if (-not $SkipTests) {
    Push-Location $root
    try {
        cmd /c '.\tests\run_tests.bat > mod\build\package_tests.txt 2>&1'
        if ($LASTEXITCODE -ne 0) { throw 'the tests failed - see mod\build\package_tests.txt' }
    } finally { Pop-Location }
}

$out = Join-Path $root 'release'
$dir = Join-Path $out $name
if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory (Join-Path $dir 'game'), (Join-Path $dir 'cockpit_tools') -Force | Out-Null
# the players' readme (English and German) on top and in the game's folder; README.md is the developers'
Copy-Item (Join-Path $root 'docs\mw4vr_readme.txt'), (Join-Path $root 'README.md'), (Join-Path $root 'LICENSE') $dir
Copy-Item (Join-Path $root 'docs\mw4vr_readme.txt') (Join-Path $dir 'game')
Copy-Item (Join-Path $root 'mod\build\d3d11.dll'), (Join-Path $root 'mod\build\dinput.dll') (Join-Path $dir 'game')
Copy-Item (Join-Path $root 'docs\mw4vr.ini.example') (Join-Path $dir 'game\mw4vr.ini')
Copy-Item (Join-Path $root 'docs\mw4vr_cockpit_map.ini') (Join-Path $dir 'game')
Copy-Item (Join-Path $root 'tools\start\mw4vr_start.cmd'), (Join-Path $root 'tools\start\mw4vr_start.ps1') (Join-Path $dir 'game')
Copy-Item (Join-Path $root 'tools\cockpit_convert.py'), (Join-Path $root 'tools\convert_all.py'), (Join-Path $root 'tools\export_cockpits.ps1') (Join-Path $dir 'cockpit_tools')
if ($Trees) {
    # the trees: models and pictures made from Poly Haven's plants (CC0), and their part of clutter.txt
    $t = Join-Path $dir 'trees\Content\Models\clutter\mw4vr'
    New-Item -ItemType Directory $t -Force | Out-Null
    Get-ChildItem $Trees -File | Where-Object { $_.Extension -in '.obj', '.material' -or $_.Name -like '*_cards.png' -or $_.Name -like '*_bark.png' } | Copy-Item -Destination $t
    Copy-Item (Join-Path $Trees 'clutter_trees.txt') (Join-Path $dir 'trees')
}

$bad = @(Get-ChildItem $dir -Recurse -File | Where-Object { $_.Extension -in '.mwc', '.gltf', '.bin', '.uasset', '.uexp', '.pak', '.psk', '.log', '.bmp' -or $_.Length -gt 30MB })
if ($bad.Count) { throw "not for a release: $($bad.FullName -join ', ')" }

$zip = Join-Path $out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $dir -DestinationPath $zip
$files = @(Get-ChildItem $dir -Recurse -File)
"{0}: {1} files, {2:N1} MB -> {3} ({4:N1} MB)" -f $name, $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), $zip, ((Get-Item $zip).Length / 1MB)
