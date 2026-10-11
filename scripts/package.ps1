# Builds InterruptPackageDeadlockFix, runs its unit tests and packages it as an installable mod:
#   dist/InterruptPackageDeadlockFix-<version>/SKSE/Plugins/InterruptPackageDeadlockFix.dll (+ readme)
#   dist/InterruptPackageDeadlockFix-<version>.zip
# The version comes from set_version() in xmake.lua.
# Run from the repo root:  powershell -ExecutionPolicy Bypass -File scripts\package.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$match = Select-String -Path (Join-Path $root 'xmake.lua') -Pattern "^set_version\('([0-9.]+)'\)"
if (-not $match) { throw 'no set_version() in xmake.lua' }
$version = $match.Matches[0].Groups[1].Value
$name = 'InterruptPackageDeadlockFix'

xmake build -P . -y $name
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
xmake build -P . -y callsite-tests
if ($LASTEXITCODE -ne 0) { throw 'test build failed' }
xmake run -P . callsite-tests
if ($LASTEXITCODE -ne 0) { throw 'unit tests failed' }

$dll = Join-Path $root "build\windows\x64\release\$name.dll"
if (-not (Test-Path $dll)) { throw "no release DLL at $dll (configure with -m release)" }
$fileVersion = (Get-Item $dll).VersionInfo.FileVersion
if ($fileVersion -ne "$version.0") { throw "DLL is $fileVersion, xmake.lua says $version" }

$stage = Join-Path $root "dist\$name-$version"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
$plugins = Join-Path $stage 'SKSE\Plugins'
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item $dll $plugins
Copy-Item (Join-Path $root "release\$name-readme.txt") $plugins

$zip = Join-Path $root "dist\$name-$version.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
& "$env:SystemRoot\System32\tar.exe" -a -cf $zip -C $stage SKSE
if ($LASTEXITCODE -ne 0) { throw 'zip failed' }

Write-Host "Packaged: $zip"
& "$env:SystemRoot\System32\tar.exe" -tvf $zip
Write-Host ("SHA-256 {0}" -f (Get-FileHash $dll -Algorithm SHA256).Hash)
