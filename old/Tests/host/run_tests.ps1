<#
.SYNOPSIS
Builds and runs deterministic Atlas host protocol tests.

.DESCRIPTION
Major functions:
- Resolve a host GCC compiler.
- Compile project-owned protocol/math sources against the minimal HAL mock.
- Run the resulting temporary executable and propagate its exit code.
#>

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$testOutput = Join-Path ([System.IO.Path]::GetTempPath()) 'atlas-host-protocol-tests.exe'
$sh2Output = Join-Path ([System.IO.Path]::GetTempPath()) 'atlas-host-sh2-timeout-tests.exe'
$bnoCevaOutput = Join-Path ([System.IO.Path]::GetTempPath()) 'atlas-host-bno-ceva-tests.exe'

$sources = @(
    'Tests/host/test_protocols.c',
    'Tests/host/test_hal_stubs.c',
    'Tests/host/test_bno085_sh2_stubs.c',
    'App/Src/atlas_adxl375.c',
    'App/Src/atlas_ble.c',
    'App/Src/atlas_bno085.c',
    'App/Src/atlas_buzzer.c',
    'App/Src/atlas_gnss.c',
    'App/Src/atlas_led.c',
    'App/Src/atlas_lsm6dsv16b.c',
    'App/Src/atlas_mmc5983ma.c',
    'App/Src/atlas_ms5611.c',
    'App/Src/atlas_rfd900x.c',
    'App/Src/atlas_rtos_policy.c',
    'App/Src/atlas_spi_device.c',
    'App/Src/atlas_status.c',
    'App/Src/atlas_time.c',
    'App/Src/atlas_uart_transport.c'
) | ForEach-Object { Join-Path $repoRoot $_ }

& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $repoRoot 'Tests/host/mocks') `
    -I (Join-Path $repoRoot 'App/Inc') `
    -I (Join-Path $repoRoot 'ThirdParty/CEVA/sh2') `
    @sources -o $testOutput
if ($LASTEXITCODE -ne 0) {
    throw "Host test compilation failed with exit code $LASTEXITCODE."
}

& $testOutput
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $repoRoot 'Tests/host/mocks') `
    -I (Join-Path $repoRoot 'App/Inc') `
    -I (Join-Path $repoRoot 'ThirdParty/CEVA/sh2') `
    (Join-Path $repoRoot 'Tests/host/test_bno085_ceva.c') `
    (Join-Path $repoRoot 'Tests/host/test_hal_stubs.c') `
    (Join-Path $repoRoot 'App/Src/atlas_bno085.c') `
    (Join-Path $repoRoot 'App/Src/atlas_time.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/sh2.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/sh2_SensorValue.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/sh2_util.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/shtp.c') -o $bnoCevaOutput
if ($LASTEXITCODE -ne 0) {
    throw "Atlas/CEVA integration test compilation failed with exit code $LASTEXITCODE."
}

& $bnoCevaOutput
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $repoRoot 'ThirdParty/CEVA/sh2') `
    (Join-Path $repoRoot 'Tests/host/test_sh2_timeout.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/sh2.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/sh2_util.c') `
    (Join-Path $repoRoot 'ThirdParty/CEVA/sh2/shtp.c') -o $sh2Output
if ($LASTEXITCODE -ne 0) {
    throw "CEVA timeout test compilation failed with exit code $LASTEXITCODE."
}

& $sh2Output
exit $LASTEXITCODE
