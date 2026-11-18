# Fetches BinSkim (Microsoft's NuGet package Microsoft.CodeAnalysis.BinSkim) for the pipeline: one fixed version,
# held against its SHA-256, unpacked into the runner's temporary folder. Writes the path of BinSkim.exe.
#   $env:BINSKIM = & tools\security\get_binskim.ps1
# A newer version: its number and the hash of its .nupkg here.
$ErrorActionPreference = 'Stop'
$version = '4.4.9.14'
$sha256 = 'ea2f48ed02e6d7ff569614d0895b80c2669fbc261c8b386639de551af049bcf4'
$tmp = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { $env:TEMP }
$dir = Join-Path $tmp "binskim-$version"
$exe = Join-Path $dir 'tools\win-x64\BinSkim.exe'
if (-not (Test-Path $exe)) {
    $pkg = Join-Path $tmp "binskim-$version.zip"
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest "https://api.nuget.org/v3-flatcontainer/microsoft.codeanalysis.binskim/$version/microsoft.codeanalysis.binskim.$version.nupkg" -OutFile $pkg
    $got = (Get-FileHash $pkg -Algorithm SHA256).Hash.ToLower()
    if ($got -ne $sha256) { throw "BinSkim $version is not the package named here: SHA-256 $got" }
    Expand-Archive $pkg $dir -Force
    Remove-Item $pkg
}
if (-not (Test-Path $exe)) { throw "no BinSkim.exe in the package ($exe)" }
$exe
