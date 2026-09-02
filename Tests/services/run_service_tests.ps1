<#
.SYNOPSIS
Runs inert production service/adapter boundary tests, without hardware access.
.DESCRIPTION
Major functions: compile against explicit HAL/register models; run assertions;
retain binaries in a unique temporary directory for reproducibility.
Pointer-width diagnostics are suppressed only for modeled 32-bit DMA addresses.
#>
$ErrorActionPreference = 'Stop'
$serviceRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$serviceOutput = Join-Path ([IO.Path]::GetTempPath()) ('atlas-services-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $serviceOutput | Out-Null
Write-Host "Service test binaries: $serviceOutput"
& gcc -std=c11 -Wall -Wextra -Werror -Wno-pointer-to-int-cast `
    -I (Join-Path $serviceRoot 'Tests/services/mocks') -I (Join-Path $serviceRoot 'App/Inc') `
    (Join-Path $serviceRoot 'Tests/services/test_io.c') `
    (Join-Path $serviceRoot 'App/Src/atlas_pyro_policy.c') `
    (Join-Path $serviceRoot 'App/Src/atlas_analog.c') -o (Join-Path $serviceOutput 'io.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'io.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $serviceRoot 'Tests/services/mocks') -I (Join-Path $serviceRoot 'App/Inc') `
    -I (Join-Path $serviceRoot 'USB_DEVICE/App') `
    -I (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Core/Inc') `
    -I (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc') `
    (Join-Path $serviceRoot 'Tests/services/test_usb_cdc.c') `
    (Join-Path $serviceRoot 'USB_DEVICE/App/usbd_cdc_if.c') `
    (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src/usbd_cdc.c') `
    (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c') `
    (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c') `
    (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c') `
    -o (Join-Path $serviceOutput 'usb_cdc.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'usb_cdc.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $serviceRoot 'Tests/services/mocks') -I (Join-Path $serviceRoot 'App/Inc') `
    -I (Join-Path $serviceRoot 'USB_DEVICE/App') `
    -I (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Core/Inc') `
    -I (Join-Path $serviceRoot 'Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc') `
    (Join-Path $serviceRoot 'Tests/services/test_usb_owner.c') `
    (Join-Path $serviceRoot 'Tests/services/service_model.c') -o (Join-Path $serviceOutput 'usb_owner.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'usb_owner.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $serviceRoot 'Tests/services/mocks') -I (Join-Path $serviceRoot 'App/Inc') `
    -I (Join-Path $serviceRoot 'FATFS/App') -I (Join-Path $serviceRoot 'FATFS/Target') `
    -I (Join-Path $serviceRoot 'Middlewares/Third_Party/FatFs/src') `
    (Join-Path $serviceRoot 'Tests/services/test_storage_owner.c') `
    (Join-Path $serviceRoot 'Tests/services/service_model.c') -o (Join-Path $serviceOutput 'storage_owner.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'storage_owner.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror `
    -I (Join-Path $serviceRoot 'Tests/services/mocks') -I (Join-Path $serviceRoot 'App/Inc') `
    (Join-Path $serviceRoot 'Tests/services/test_expansion.c') `
    (Join-Path $serviceRoot 'Tests/services/service_model.c') `
    (Join-Path $serviceRoot 'App/Src/atlas_spi_device.c') -o (Join-Path $serviceOutput 'expansion.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'expansion.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& gcc -std=c11 -Wall -Wextra -Werror `
    (Join-Path $serviceRoot 'Tests/services/test_sysmem.c') -o (Join-Path $serviceOutput 'sysmem.exe')
if ($LASTEXITCODE -ne 0) { exit 2 }
& (Join-Path $serviceOutput 'sysmem.exe')
exit $LASTEXITCODE
