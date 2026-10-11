# Builds InterruptPackageDeadlockFix and packages it as an installable mod:
#   dist/InterruptPackageDeadlockFix-1.1.0/SKSE/Plugins/InterruptPackageDeadlockFix.dll (+ readme)
#   dist/InterruptPackageDeadlockFix-1.1.0.zip
# Run from the repo root:  powershell -ExecutionPolicy Bypass -File scripts\package.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

xmake build -P . -y InterruptPackageDeadlockFix
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

$dll = Join-Path $root 'build\windows\x64\release\InterruptPackageDeadlockFix.dll'
$stage = Join-Path $root 'dist\InterruptPackageDeadlockFix-1.1.0'
$plugins = Join-Path $stage 'SKSE\Plugins'
New-Item -ItemType Directory -Force $plugins | Out-Null
Copy-Item -Force $dll $plugins
Copy-Item -Force (Join-Path $root 'release\InterruptPackageDeadlockFix-readme.txt') $plugins

$zip = Join-Path $root 'dist\InterruptPackageDeadlockFix-1.1.0.zip'
& "$env:SystemRoot\System32\tar.exe" -a -cf $zip -C $stage .
if ($LASTEXITCODE -ne 0) { throw 'zip failed' }
Write-Host "Packaged: $zip"
