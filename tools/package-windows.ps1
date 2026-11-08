<#
.SYNOPSIS
  Builds the Release desktop app and creates a self-contained Windows package with windeployqt.

.DESCRIPTION
  1. cmake --preset windows-release, build, ctest (the package is only made from a tested build)
  2. copies isuzu_diagnostic.exe + data/isuzu-knowledge.db into dist/IsuzuDiagnostic-<version>/
  3. runs windeployqt (Qt DLLs, platform/style/sqldriver plugins, MinGW runtime)
  4. writes PACKAGE.txt (version, git commit, Qt version, knowledge DB SHA-256)
  5. zips the folder

  Nothing is flashed and no device is opened. Usage (PowerShell):

    tools/package-windows.ps1 -QtRoot C:/Qt/6.11.1/mingw_64 -MinGwBin C:/Qt/Tools/mingw1310_64/bin -NinjaDir C:/Qt/Tools/Ninja
#>
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$MinGwBin,
    [Parameter(Mandatory = $true)][string]$NinjaDir,
    [switch]$SkipTests
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..')
Set-Location $repo

$env:QT_ROOT = $QtRoot
$env:PATH = "$QtRoot\bin;$MinGwBin;$NinjaDir;$env:PATH"

cmake --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
cmake --build --preset windows-release
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
if (-not $SkipTests) {
    ctest --preset windows-release
    if ($LASTEXITCODE -ne 0) { throw 'tests failed – no package is created from a failing build' }
}

$version = (Select-String -Path CMakeLists.txt -Pattern 'VERSION (\d+\.\d+\.\d+)').Matches[0].Groups[1].Value
$commit = (git rev-parse --short HEAD).Trim()
$dirty = if (git status --porcelain) { '-dirty' } else { '' }
$name = "IsuzuDiagnostic-$version-$commit$dirty-win64"
$dist = Join-Path $repo "dist\$name"
if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force (Join-Path $dist 'data') | Out-Null

$exe = 'build\windows-release\apps\desktop-qt\isuzu_diagnostic.exe'
Copy-Item $exe $dist
Copy-Item 'data\knowledge\isuzu-knowledge.db' (Join-Path $dist 'data')

# --compiler-runtime copies libgcc/libstdc++/libwinpthread from the MinGW on PATH.
# windeployqt prints harmless warnings (e.g. no dxcompiler.dll) on stderr; Windows PowerShell 5.1
# would turn them into terminating errors under 'Stop', so judge the call by its exit code only.
$ErrorActionPreference = 'Continue'
# Only QSQLITE is used; the other SQL drivers would add unused client-library dependencies.
& "$QtRoot\bin\windeployqt.exe" --release --compiler-runtime --no-translations `
    --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql (Join-Path $dist 'isuzu_diagnostic.exe') 2>&1 |
    ForEach-Object { "$_" } | Where-Object { $_ -match 'Warning|Error' } | ForEach-Object { Write-Host "windeployqt: $_" }
$deployExit = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($deployExit -ne 0) { throw "windeployqt failed ($deployExit)" }

$qtVersion = (& "$QtRoot\bin\qtpaths.exe" --qt-version).Trim()
$dbHash = (Get-FileHash (Join-Path $dist 'data\isuzu-knowledge.db') -Algorithm SHA256).Hash.ToLower()
@"
Isuzu Diagnostic System $version
commit:        $commit$dirty
built:         $(Get-Date -Format s)
Qt:            $qtVersion (MinGW)
knowledge DB:  data/isuzu-knowledge.db  sha256 $dbHash

Implemented but NOT vehicle-validated. Mode 04 (clear DTCs) is not available.
SIMULATION results are labelled on every screen and are not vehicle data.
"@ | Set-Content -Encoding utf8 (Join-Path $dist 'PACKAGE.txt')

$zip = "$dist.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $dist -DestinationPath $zip
Write-Host "package: $zip"
