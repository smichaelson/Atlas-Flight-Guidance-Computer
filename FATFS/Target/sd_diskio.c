/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @brief   SD Disk I/O driver
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/**
 * Atlas-owned polling FatFs adapter.
 * Major functions: SD_initialize()/status(), SD_read()/write(), SD_ioctl().
 * Single storage-task ownership replaces the generated unscoped DMA callbacks.
 * One sector at a time bounds FIFO service; the low-priority owner can be
 * preempted by control/sensor tasks. Higher-priority load may cause a safe I/O
 * timeout: this is a correctness-first baseline, not a throughput guarantee.
 */
#include "ff_gen_drv.h"
#include "sd_diskio.h"
#include "atlas_time.h"
#include <stdbool.h>
#include <string.h>

#define SD_SECTOR_BYTES (512U)
#define SD_TRANSFER_TIMEOUT_MS (250U)
#define SD_READY_TIMEOUT_MS (1000U)

static DSTATUS sd_status = STA_NOINIT;
static bool sd_mount_authorized;
static BSP_SD_CardInfo sd_info;
/* ST's diskio glue caches initialization separately from DSTATUS. Its cache
 * must be invalidated too, otherwise an explicit remount skips our real init. */
extern Disk_drvTypeDef disk;

/** @brief Reset media/controller after any ambiguous operation result. */
void SD_Invalidate(void)
{
    sd_mount_authorized = false;
    for (unsigned i = 0U; i < _VOLUMES; ++i)
        if (disk.drv[i] == &SD_Driver) disk.is_initialized[i] = 0U;
    BSP_SD_DeInit();
    sd_status = STA_NOINIT;
    memset(&sd_info, 0, sizeof(sd_info));
}
/** @brief Only the storage owner's MOUNT path may permit a new card session. */
void SD_PrepareMount(void)
{
    SD_Invalidate();
    sd_mount_authorized = true;
}

/** @brief Await card readiness with yielding, bounded retries. @return true when ready. */
static bool sd_wait_ready(void)
{
    const uint32_t start = HAL_GetTick();
    do
    {
        const uint8_t state = BSP_SD_GetCardState();
        if (state == SD_TRANSFER_OK) return true;
        if (state == SD_TRANSFER_ERROR) return false;
        AtlasTime_DelayMs(1U);
    } while ((uint32_t)(HAL_GetTick() - start) < SD_READY_TIMEOUT_MS);
    return false;
}

/** @brief Initialize optional volume zero. @param lun Logical unit. @return Disk status. */
static DSTATUS SD_initialize(BYTE lun)
{
    if (lun != 0U || !sd_mount_authorized) return STA_NOINIT;
    sd_mount_authorized = false;
    if (BSP_SD_Init() != MSD_OK ||
        BSP_SD_GetCardInfo(&sd_info) != MSD_OK ||
        sd_info.LogBlockSize != SD_SECTOR_BYTES || sd_info.LogBlockNbr == 0U ||
        !sd_wait_ready())
    {
        SD_Invalidate();
        return STA_NOINIT;
    }
    sd_status = 0U;
    return sd_status;
}

/** @brief Reject a replaced card before FatFs can reuse cached metadata.
 * @param lun Unit. @return Current disk status, without implicit remount authority. */
static DSTATUS SD_status(BYTE lun)
{
    if (lun == 0U && (sd_status & STA_NOINIT) == 0U && !BSP_SD_IsMediaCurrent()) SD_Invalidate();
    return lun == 0U ? sd_status : STA_NOINIT;
}

/**
 * @brief Validate an LBA interval without overflow.
 * @param lun Unit. @param buffer Caller storage. @param sector First LBA.
 * @param count Number of sectors. @return FatFs result.
 */
static DRESULT sd_validate(BYTE lun, const BYTE *buffer, DWORD sector, UINT count)
{
    if (lun != 0U || buffer == NULL || count == 0U) return RES_PARERR;
    if ((SD_status(lun) & STA_NOINIT) != 0U) return RES_NOTRDY;
    if (sector >= sd_info.LogBlockNbr ||
        count > sd_info.LogBlockNbr - sector) return RES_PARERR;
    return RES_OK;
}

/**
 * @brief Read via an aligned CPU-only sector buffer, including unaligned callers.
 * @param lun Unit. @param buffer Destination. @param sector LBA. @param count Sectors.
 * @return RES_OK only after every sector and final card-ready check succeed.
 */
static DRESULT SD_read(BYTE lun, BYTE *buffer, DWORD sector, UINT count)
{
    uint32_t scratch[SD_SECTOR_BYTES / sizeof(uint32_t)];
    DRESULT result = sd_validate(lun, buffer, sector, count);
    if (result != RES_OK) return result;
    for (UINT i = 0U; i < count; ++i)
    {
        if (!sd_wait_ready() ||
            BSP_SD_ReadBlocks(scratch, sector + i, 1U, SD_TRANSFER_TIMEOUT_MS) != MSD_OK ||
            !sd_wait_ready())
        {
            SD_Invalidate();
            return RES_ERROR;
        }
        memcpy(buffer, scratch, sizeof(scratch));
        buffer += SD_SECTOR_BYTES;
        if (i + 1U < count) AtlasTime_DelayMs(1U);
    }
    return RES_OK;
}

#if _USE_WRITE == 1
/**
 * @brief Write with synchronous buffer lifetime and post-programming readiness.
 * @param lun Unit. @param buffer Source. @param sector LBA. @param count Sectors.
 * @return FatFs result; an error may leave partially written media, never live DMA.
 */
static DRESULT SD_write(BYTE lun, const BYTE *buffer, DWORD sector, UINT count)
{
    uint32_t scratch[SD_SECTOR_BYTES / sizeof(uint32_t)];
    DRESULT result = sd_validate(lun, buffer, sector, count);
    if (result != RES_OK) return result;
    for (UINT i = 0U; i < count; ++i)
    {
        memcpy(scratch, buffer, sizeof(scratch));
        if (!sd_wait_ready() ||
            BSP_SD_WriteBlocks(scratch, sector + i, 1U, SD_TRANSFER_TIMEOUT_MS) != MSD_OK ||
            !sd_wait_ready())
        {
            SD_Invalidate();
            return RES_ERROR;
        }
        buffer += SD_SECTOR_BYTES;
        if (i + 1U < count) AtlasTime_DelayMs(1U);
    }
    return RES_OK;
}
#endif

#if _USE_IOCTL == 1
/** @brief Implement required FatFs controls. @param lun Unit. @param cmd Operation.
 * @param buffer Typed result destination (unused for CTRL_SYNC). @return FatFs result. */
static DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buffer)
{
    if (lun != 0U) return RES_PARERR;
    if ((SD_status(lun) & STA_NOINIT) != 0U) return RES_NOTRDY;
    if (cmd == CTRL_SYNC)
    {
        if (sd_wait_ready()) return RES_OK;
        SD_Invalidate();
        return RES_ERROR;
    }
    if (buffer == NULL) return RES_PARERR;
    switch (cmd)
    {
        case GET_SECTOR_COUNT: *(DWORD *)buffer = sd_info.LogBlockNbr; return RES_OK;
        case GET_SECTOR_SIZE: *(WORD *)buffer = SD_SECTOR_BYTES; return RES_OK;
        /* Unknown erase geometry: conservative one-sector allocation hint.
           This is not an erase command or a physical erase-size assertion. */
        case GET_BLOCK_SIZE: *(DWORD *)buffer = 1U; return RES_OK;
        default: return RES_PARERR;
    }
}
#endif

const Diskio_drvTypeDef SD_Driver = {
    SD_initialize, SD_status, SD_read,
#if _USE_WRITE == 1
    SD_write,
#endif
#if _USE_IOCTL == 1
    SD_ioctl,
#endif
};
