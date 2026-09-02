/**
 * @file test_sd_integration.c
 * @brief Real BSP/disk/FatFs acceptance on a synthetic RAM card; no physical media.
 * Major functions: main() checks configuration, copy ownership, media changes,
 * timeout/reset, range validation and an actual mount/append/sync/read lifecycle.
 * R04/R05's old callback model is replaced because production no longer uses IDMA.
 */
#include "ff_gen_drv.h"
#include "bsp_driver_sd.h"
#include "sd_diskio.h"
#include "atlas_time.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { printf("FAIL SD line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define SECTORS 128U
SD_HandleTypeDef hsd1;
GPIO_TypeDef atlas_test_gpio_d;
uint32_t review_sd_instance;
static uint8_t media[SECTORS][512];
static uint32_t tick, delays, reset_count, io_count;
static bool present = true, bad_handle, timeout_io, busy_card, remove_during_io, fail_init;

/** @brief Logical clock. @return Monotonic milliseconds. */
uint32_t HAL_GetTick(void) { return tick++; }
/** @brief Model a yielding wait. @param ms Minimum requested delay. */
void AtlasTime_DelayMs(uint32_t ms) { tick += ms; ++delays; }
/** @brief Model controller reset without touching media. */
void AtlasReview_SdReset(void) { ++reset_count; }
/** @brief Model normally open card switch. @param port Port. @param pin Pin.
 * @return Low only with media inserted. */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ (void)port; (void)pin; return present ? GPIO_PIN_RESET : GPIO_PIN_SET; }
/** @brief Validate all HAL entry settings. @param sd Handle. @return Mock status. */
HAL_StatusTypeDef HAL_SD_Init(SD_HandleTypeDef *sd)
{
    bad_handle = sd->Instance != SDMMC1 || sd->Init.ClockEdge != SDMMC_CLOCK_EDGE_RISING ||
        sd->Init.BusWide != SDMMC_BUS_WIDE_1B || sd->Init.ClockDiv != 1U ||
        sd->Init.HardwareFlowControl != SDMMC_HARDWARE_FLOW_CONTROL_ENABLE ||
        sd->Init.ClockPowerSave != SDMMC_CLOCK_POWER_SAVE_DISABLE;
    return bad_handle || fail_init ? HAL_ERROR : HAL_OK;
}
/** @brief Model card/host width agreement. @param sd Handle. @param width Width.
 * @return HAL status. */
HAL_StatusTypeDef HAL_SD_ConfigWideBusOperation(SD_HandleTypeDef *sd, uint32_t width)
{ (void)sd; return width == SDMMC_BUS_WIDE_4B ? HAL_OK : HAL_ERROR; }
/** @brief Supply readiness. @param sd Handle. @return Card state. */
uint32_t HAL_SD_GetCardState(SD_HandleTypeDef *sd)
{ (void)sd; return busy_card ? HAL_SD_CARD_PROGRAMMING : HAL_SD_CARD_TRANSFER; }
/** @brief Execute a synchronous RAM read. @param sd Handle. @param data Destination.
 * @param address LBA. @param count Sectors. @param timeout Bound. @return HAL result. */
HAL_StatusTypeDef HAL_SD_ReadBlocks(SD_HandleTypeDef *sd, uint8_t *data,
                                   uint32_t address, uint32_t count, uint32_t timeout)
{
    (void)sd;
    CHECK(count == 1U && address < SECTORS && timeout <= 250U && timeout != 0U);
    CHECK(((uintptr_t)data & 3U) == 0U);
    ++io_count;
    if (remove_during_io) { present = false; BSP_SD_DetectFromISR(); return HAL_ERROR; }
    if (timeout_io) { tick += timeout; return HAL_TIMEOUT; }
    memcpy(data, media[address], 512);
    return HAL_OK;
}
/** @brief Execute a synchronous RAM write. @param sd Handle. @param data Source.
 * @param address LBA. @param count Sectors. @param timeout Bound. @return HAL result. */
HAL_StatusTypeDef HAL_SD_WriteBlocks(SD_HandleTypeDef *sd, uint8_t *data,
                                    uint32_t address, uint32_t count, uint32_t timeout)
{
    (void)sd;
    CHECK(count == 1U && address < SECTORS && timeout <= 250U);
    ++io_count;
    if (timeout_io) { tick += timeout; return HAL_TIMEOUT; }
    memcpy(media[address], data, 512);
    return HAL_OK;
}
/** @brief Supply synthetic geometry. @param sd Handle. @param info Destination.
 * @return HAL_OK. */
HAL_StatusTypeDef HAL_SD_GetCardInfo(SD_HandleTypeDef *sd, HAL_SD_CardInfoTypeDef *info)
{ (void)sd; info->LogBlockNbr = SECTORS; info->LogBlockSize = 512U; return HAL_OK; }
/** @brief Supply a valid known-unknown file date. @return FAT date. */
DWORD get_fattime(void) { return (1UL << 21) | (1UL << 16); }
/** @brief Construct a minimal FAT12 volume in RAM; not a format operation on real media. */
static void make_ram_volume(void)
{
    memset(media, 0, sizeof(media));
    uint8_t *b = media[0];
    b[0] = 0xEB; b[1] = 0x3C; b[2] = 0x90;
    memcpy(b + 3, "ATLASTST", 8);
    b[11] = 0; b[12] = 2; b[13] = 1; b[14] = 1; b[16] = 1;
    b[17] = 32; b[19] = SECTORS; b[21] = 0xF8; b[22] = 1;
    b[24] = 1; b[26] = 1; b[38] = 0x29;
    memcpy(b + 54, "FAT12   ", 8);
    b[510] = 0x55; b[511] = 0xAA;
    media[1][0] = 0xF8; media[1][1] = media[1][2] = 0xFF;
}
/** @brief Exercise corrected contracts and the actual filesystem. @return Test result. */
int main(void)
{
    uint8_t unaligned[1025];
    memset(media[7], 0xA5, 512);
    CHECK(SD_Driver.disk_initialize(0) != 0); /* No implicit first mount. */
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) == 0 && !bad_handle);
    puts("PASS R03: fully configured SD handle, initial 1-bit and negotiated 4-bit bus");
    CHECK(SD_Driver.disk_read(0, unaligned + 1, 7, 1) == RES_OK);
    CHECK(unaligned[1] == 0xA5 && unaligned[512] == 0xA5);
    memset(unaligned + 1, 0x3C, 512);
    CHECK(SD_Driver.disk_write(0, unaligned + 1, 8, 1) == RES_OK);
    CHECK(media[8][0] == 0x3C);
    puts("PASS R04/R06: synchronous immediate completion and unaligned CPU buffers");

    const uint32_t before = io_count;
    CHECK(SD_Driver.disk_read(0, unaligned, UINT32_MAX, 2) == RES_PARERR);
    CHECK(SD_Driver.disk_read(0, unaligned, SECTORS - 1, 2) == RES_PARERR);
    CHECK(SD_Driver.disk_read(1, unaligned, 0, 1) == RES_PARERR);
    CHECK(SD_Driver.disk_read(0, NULL, 0, 1) == RES_PARERR);
    CHECK(SD_Driver.disk_read(0, unaligned, 0, 0) == RES_PARERR && io_count == before);

    timeout_io = true;
    const uint32_t reset_before = reset_count;
    CHECK(SD_Driver.disk_read(0, unaligned, 0, 1) == RES_ERROR);
    CHECK(reset_count > reset_before && hsd1.Instance == NULL);
    CHECK(SD_Driver.disk_read(0, unaligned, 0, 1) == RES_NOTRDY);
    timeout_io = false;
    CHECK(SD_Driver.disk_initialize(0) != 0); /* Fault consumes old mount authority. */
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) == 0);
    busy_card = true;
    const uint32_t wait_start = tick;
    CHECK(SD_Driver.disk_ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);
    CHECK(tick - wait_start < 1100U && delays != 0U);
    busy_card = false;
    puts("PASS R05: bounded readiness waits, timeout reset, no retained DMA ownership");

    present = false;
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) != 0);
    present = true;
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) == 0);
    BSP_SD_DetectFromISR(); BSP_SD_DetectFromISR(); /* Remove/reinsert before polling. */
    CHECK(SD_Driver.disk_read(0, unaligned, 0, 1) == RES_NOTRDY);
    CHECK(SD_Driver.disk_initialize(0) != 0);
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) == 0);
    remove_during_io = true;
    CHECK(SD_Driver.disk_read(0, unaligned, 0, 1) == RES_ERROR);
    remove_during_io = false; present = true;
    fail_init = true;
    SD_PrepareMount();
    CHECK(SD_Driver.disk_initialize(0) != 0 && hsd1.Instance == NULL);
    fail_init = false;
    puts("PASS SD: absent card, rapid replacement, mid-transfer removal, failed init");

    make_ram_volume();
    FATFS volume;
    FIL file;
    char path[4], readback[8] = {0};
    UINT count;
    CHECK(FATFS_LinkDriver(&SD_Driver, path) == 0);
    SD_PrepareMount();
    CHECK(f_mount(&volume, path, 1) == FR_OK);
    CHECK(f_open(&file, "0:/TEST.BIN", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK);
    CHECK(f_write(&file, "abc", 3, &count) == FR_OK && count == 3);
    CHECK(f_sync(&file) == FR_OK && f_close(&file) == FR_OK);
    CHECK(f_open(&file, "0:/TEST.BIN", FA_WRITE | FA_OPEN_ALWAYS) == FR_OK);
    CHECK(f_lseek(&file, f_size(&file)) == FR_OK);
    CHECK(f_write(&file, "de", 2, &count) == FR_OK && count == 2);
    CHECK(f_close(&file) == FR_OK);
    CHECK(f_open(&file, "0:/TEST.BIN", FA_READ) == FR_OK);
    CHECK(f_read(&file, readback, sizeof(readback), &count) == FR_OK && count == 5);
    CHECK(memcmp(readback, "abcde", 5) == 0 && f_close(&file) == FR_OK);
    BSP_SD_DetectFromISR(); BSP_SD_DetectFromISR();
    const uint32_t before_implicit = io_count;
    CHECK(f_open(&file, "0:/TEST.BIN", FA_WRITE | FA_OPEN_ALWAYS) == FR_NOT_READY);
    CHECK(io_count == before_implicit); /* Cached FAT cannot redirect a write. */
    CHECK(SD_Driver.disk_initialize(0) != 0);
    CHECK(f_mount(NULL, path, 0) == FR_OK);
    SD_PrepareMount();
    CHECK(f_mount(&volume, path, 1) == FR_OK); /* ST glue cache must not skip init. */
    CHECK(f_open(&file, "0:/TEST.BIN", FA_READ) == FR_OK);
    CHECK(f_read(&file, readback, sizeof(readback), &count) == FR_OK && count == 5);
    CHECK(memcmp(readback, "abcde", 5) == 0 && f_close(&file) == FR_OK);
    CHECK(f_mount(NULL, path, 0) == FR_OK);
    puts("PASS SD: real FatFs mount, create, append-preserves-existing, sync, close, read");
    puts("PASS SD: replacement blocks implicit FatFs reinitialization and cached-file writes");
    return 0;
}
