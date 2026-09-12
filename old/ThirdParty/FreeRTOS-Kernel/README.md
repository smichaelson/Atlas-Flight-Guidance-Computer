# FreeRTOS Kernel subset

This directory contains the unmodified files needed by Atlas from **FreeRTOS Kernel V10.6.2**, copied from ST's locally installed `STM32Cube_FW_H7_V1.13.0` package. That pairing keeps the RTOS port aligned with the repository's STM32CubeH7 baseline.

Included here are the kernel task, queue, list, and stream-buffer implementations; public headers; and the Cortex-M7 r0p1 ports for GNU Arm Embedded and IAR. The r0p1 port is intentionally used because it includes the Cortex-M7 erratum workaround and is valid when the processor revision has not yet been physically confirmed.

Atlas uses the native FreeRTOS API and static allocation only. No `heap_*.c` allocator is included. Project-specific configuration is in `App/Inc/FreeRTOSConfig.h`.

The upstream files remain under the MIT license in [`LICENSE`](LICENSE). Do not edit vendor files in place; replace this subset from a reviewed upstream/STM32Cube release and repeat both target builds and the RTOS review checklist.
