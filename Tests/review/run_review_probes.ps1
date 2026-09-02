<#
.SYNOPSIS
Runs host-only acceptance probes for findings in docs/REVIEW_REPORT.md.

.DESCRIPTION
Major functions:
- Compile the real firmware/adapter sources against focused boundary models.
- Run positive controls, then tick-phase, shared-timer, parser, and SD fault probes.
- Return 1 while behavioral findings remain open; return 2 for harness/build errors.

These intentionally expose gaps that the ordinary protocol suite does not test.
They do not flash a board or access an SD card, USB device, radio, or output pin.
Build products go to a unique temporary directory printed for investigation.
The corrected code must pass; a pass is not hardware qualification.
#>
$ErrorActionPreference = 'Stop'
try {
    $reviewCompiler = Get-Command -Name gcc -CommandType Application -ErrorAction Stop
    $reviewRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
    $reviewOutput = Join-Path ([IO.Path]::GetTempPath()) ('atlas-review-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $reviewOutput | Out-Null
    Write-Host "Review probe binaries: $reviewOutput"
    $reviewIncludes = @(
        '-I', (Join-Path $reviewRoot 'Tests/review/mocks'),
        '-I', (Join-Path $reviewRoot 'Tests/host/mocks'),
        '-I', (Join-Path $reviewRoot 'App/Inc'),
        '-I', (Join-Path $reviewRoot 'FATFS/Target'),
        '-I', (Join-Path $reviewRoot 'Middlewares/Third_Party/FatFs/src')
    )
    $reviewCases = @(
        @{
            Name = 'analog'; Flags = @(); Sources = @(
                'Tests/review/test_analog.c', 'App/Src/atlas_analog.c')
        },
        @{
            Name = 'pyro_policy'; Flags = @(); Sources = @(
                'Tests/review/test_pyro_policy.c', 'App/Src/atlas_pyro_policy.c')
        },
        @{
            Name = 'timing'; Flags = @('-DATLAS_USE_FREERTOS=1'); Sources = @(
                'Tests/review/test_timing.c', 'App/Src/atlas_time.c')
        },
        @{
            Name = 'timing_100hz'; Flags = @('-DATLAS_USE_FREERTOS=1', '-DconfigTICK_RATE_HZ=100'); Sources = @(
                'Tests/review/test_timing.c', 'App/Src/atlas_time.c')
        },
        @{
            Name = 'timing_2000hz'; Flags = @('-DATLAS_USE_FREERTOS=1', '-DconfigTICK_RATE_HZ=2000'); Sources = @(
                'Tests/review/test_timing.c', 'App/Src/atlas_time.c')
        },
        @{
            Name = 'gnss'; Flags = @(); Sources = @(
                'Tests/review/test_gnss_integration.c', 'App/Src/atlas_gnss.c',
                'App/Src/atlas_uart_transport.c', 'App/Src/atlas_time.c', 'App/Src/atlas_status.c')
        },
        @{
            # The bundled ff_gen_drv.c has an unused lun parameter; all other warnings fail.
            Name = 'sd'; Flags = @('-Wno-unused-parameter'); Sources = @(
                'Tests/review/test_sd_integration.c', 'FATFS/Target/bsp_driver_sd.c',
                'FATFS/Target/sd_diskio.c', 'Middlewares/Third_Party/FatFs/src/ff.c',
                'Middlewares/Third_Party/FatFs/src/ff_gen_drv.c',
                'Middlewares/Third_Party/FatFs/src/diskio.c')
        }
    )
    $reviewFailedCases = 0
    foreach ($reviewCase in $reviewCases) {
        $reviewSources = @($reviewCase.Sources | ForEach-Object { Join-Path $reviewRoot $_ })
        $reviewBinary = Join-Path $reviewOutput ($reviewCase.Name + '.exe')
        & $reviewCompiler.Source -std=c11 -Wall -Wextra -Werror @reviewIncludes @($reviewCase.Flags) @reviewSources -o $reviewBinary
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Review probe compilation failed: $($reviewCase.Name)" -ErrorAction Continue
            exit 2
        }
        & $reviewBinary
        if ($LASTEXITCODE -eq 1) { ++$reviewFailedCases }
        elseif ($LASTEXITCODE -ne 0) {
            Write-Error "Review probe harness/runtime failed: $($reviewCase.Name), exit $LASTEXITCODE" -ErrorAction Continue
            exit 2
        }
    }
    if ($reviewFailedCases -ne 0) {
        Write-Host "REVIEW GAPS OPEN: $reviewFailedCases probe groups failed acceptance. See docs/REVIEW_REPORT.md."
        exit 1
    }
    Write-Host 'All focused review probes passed; physical qualification remains separate.'
    exit 0
}
catch {
    Write-Error ("Review probe harness failed: " + $_.Exception.Message) -ErrorAction Continue
    exit 2
}
