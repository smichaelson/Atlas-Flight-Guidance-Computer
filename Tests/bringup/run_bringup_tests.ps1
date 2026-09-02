<#
.SYNOPSIS
Runs offline diagnostic protocol and production IO/storage profile tests.
.DESCRIPTION
Major operations: compile strict C tests; exercise both service safeguards;
run the dashboard model and hidden Tk UI. No serial/device/programming access.
.PARAMETER Python
Python 3.10+ executable with Tk; pyserial is NOT required for these inert tests.
#>
param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$bringupRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$bringupOutput = Join-Path ([IO.Path]::GetTempPath()) ('atlas-bringup-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $bringupOutput | Out-Null
Write-Host "Inert bring-up tests: $bringupOutput"
& gcc -std=c11 -Wall -Wextra -Werror -I (Join-Path $bringupRoot 'App/Inc') `
    (Join-Path $bringupRoot 'Tests/bringup/test_protocol.c') `
    (Join-Path $bringupRoot 'App/Src/atlas_bringup_protocol.c') -lm -o (Join-Path $bringupOutput 'protocol.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $bringupOutput 'protocol.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast -DATLAS_BRINGUP=1 `
    -I (Join-Path $bringupRoot 'Tests/services/mocks') -I (Join-Path $bringupRoot 'App/Inc') `
    (Join-Path $bringupRoot 'Tests/services/test_io.c') `
    (Join-Path $bringupRoot 'App/Src/atlas_pyro_policy.c') `
    (Join-Path $bringupRoot 'App/Src/atlas_analog.c') -o (Join-Path $bringupOutput 'io.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $bringupOutput 'io.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror -DATLAS_BRINGUP=1 `
    -I (Join-Path $bringupRoot 'Tests/services/mocks') -I (Join-Path $bringupRoot 'App/Inc') `
    -I (Join-Path $bringupRoot 'FATFS/App') -I (Join-Path $bringupRoot 'FATFS/Target') `
    -I (Join-Path $bringupRoot 'Middlewares/Third_Party/FatFs/src') `
    (Join-Path $bringupRoot 'Tests/services/test_storage_owner.c') `
    (Join-Path $bringupRoot 'Tests/services/service_model.c') -o (Join-Path $bringupOutput 'storage.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $bringupOutput 'storage.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -O2 -Wall -Wextra -Werror -DATLAS_BRINGUP=1 `
    -I (Join-Path $bringupRoot 'Tests/services/mocks') -I (Join-Path $bringupRoot 'App/Inc') `
    -I (Join-Path $bringupRoot 'ThirdParty/CEVA/sh2') `
    (Join-Path $bringupRoot 'Tests/bringup/test_console.c') `
    (Join-Path $bringupRoot 'Tests/services/service_model.c') `
    (Join-Path $bringupRoot 'App/Src/atlas_bringup_protocol.c') `
    (Join-Path $bringupRoot 'App/Src/atlas_status.c') -lm -o (Join-Path $bringupOutput 'console.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
$env:ATLAS_BRINGUP_TEST_DIR = $bringupOutput
$env:ATLAS_CONSOLE_TEST_EXE = Join-Path $bringupOutput 'console.exe'
& $Python (Join-Path $bringupRoot 'Tests/bringup/test_dashboard.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Python (Join-Path $bringupRoot 'tools/bringup/dashboard.py') --smoke-test
exit $LASTEXITCODE
