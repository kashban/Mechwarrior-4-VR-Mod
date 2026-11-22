# The mechs' own arms as a release's attachment:
#   powershell -File tools\pack_arms.ps1 -Game <the game's folder> -Version v11.95
# packs <game>\mw4vr_arms\*.mwa (the files the mod made, mod\srcrms.cpp) into release\MW4VR-arms-<version>.zip.
# The files are the game's own geometry: they are not in the repository, the pipeline does not have them - the zip
# is uploaded by hand (gh release upload <tag> <zip>). The setup installs it when it lies beside the package.
param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][string]$Version
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $Game 'mw4vr_arms'
$files = @(Get-ChildItem $src -Filter *.mwa -ErrorAction SilentlyContinue)
if (-not $files.Count) { throw "no .mwa files in $src" }
$out = Join-Path $root 'release'
New-Item -ItemType Directory -Force $out | Out-Null
$zip = Join-Path $out "MW4VR-arms-$Version.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $files.FullName -DestinationPath $zip -CompressionLevel Optimal
'{0} files, {1:N0} MB -> {2} ({3:N0} MB)' -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB), $zip, ((Get-Item $zip).Length / 1MB)
