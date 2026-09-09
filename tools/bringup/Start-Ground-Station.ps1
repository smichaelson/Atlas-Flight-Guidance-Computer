<#
.SYNOPSIS
Opens the local Atlas dashboard; no automatic connection, probe or firmware write.
.PARAMETER Demo
Open labelled simulated instruments instead of the disconnected hardware view.
.PARAMETER Python
Optional Python executable; defaults to the project's existing virtual environment.
#>
param([switch]$Demo, [string]$Python = '', [int]$Port = 8765)
$ErrorActionPreference = 'Stop'
$atlasRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (-not $Python) { $Python = Join-Path $atlasRoot '.venv/Scripts/python.exe' }
if (-not (Test-Path -LiteralPath $Python)) { throw 'Create .venv and install tools/bringup/requirements.txt first.' }
$atlasUrl = "http://127.0.0.1:$Port/"
$atlasAlreadyRunning = $false
try {
    $atlasPage = Invoke-WebRequest -Uri $atlasUrl -UseBasicParsing -TimeoutSec 2
    $atlasAlreadyRunning = $atlasPage.Content.Contains('<meta name="atlas-token"')
} catch { }
if ($atlasAlreadyRunning) {
    Write-Host 'Atlas is already running. Opening its current session; use Explore demo if needed.'
    Start-Process -FilePath $atlasUrl
    exit 0
}
$atlasArgs = @((Join-Path $PSScriptRoot 'ground_station.py'), '--open', '--port', "$Port")
$atlasManifest = Join-Path $atlasRoot 'build/BenchMake/Atlas-Bringup.manifest.json'
if (Test-Path -LiteralPath $atlasManifest) { $atlasArgs += @('--manifest', $atlasManifest) }
if ($Demo) { $atlasArgs += '--demo' }
& $Python @atlasArgs
exit $LASTEXITCODE
