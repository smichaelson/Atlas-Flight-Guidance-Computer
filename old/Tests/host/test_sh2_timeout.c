/**
 * @file test_sh2_timeout.c
 * @brief Actual CEVA-stack regression for bounded BNO085 product-ID discovery.
 *
 * Major functions:
 * - test_read()/test_write(): model a silent hub after accepting the request.
 * - test_time_us(): advances deterministic time so the operation bound is observable.
 * - main(): proves sh2_getProdIds() returns TIMEOUT instead of spinning forever.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "sh2.h"
#include "sh2_err.h"
#include "sh2_hal.h"

static uint32_t test_now_us;
static uint32_t test_reads;
static uint32_t test_writes;

/** @brief Accept virtual transport startup. */
static int test_open(sh2_Hal_t *hal)
{
    (void)hal;
    return SH2_OK;
}

/** @brief Complete virtual transport shutdown. */
static void test_close(sh2_Hal_t *hal)
{
    (void)hal;
}

/** @brief Model a hub that never supplies the requested response. */
static int test_read(sh2_Hal_t *hal, uint8_t *data, unsigned length,
                     uint32_t *timestamp_us)
{
    (void)hal;
    (void)data;
    (void)length;
    (void)timestamp_us;
    ++test_reads;
    return 0;
}

/** @brief Accept the product-ID request so only response timeout is exercised. */
static int test_write(sh2_Hal_t *hal, uint8_t *data, unsigned length)
{
    (void)hal;
    (void)data;
    ++test_writes;
    return (int)length;
}

/** @brief Advance 100 ms per observation without sleeping. */
static uint32_t test_time_us(sh2_Hal_t *hal)
{
    (void)hal;
    test_now_us += 100000U;
    return test_now_us;
}

/** @brief Prove the locally patched product-ID operation is finite. */
int main(void)
{
    sh2_Hal_t hal = {
        .open = test_open,
        .close = test_close,
        .read = test_read,
        .write = test_write,
        .getTimeUs = test_time_us,
    };
    sh2_ProductIds_t product_ids;

    assert(sh2_open(&hal, NULL, NULL) == SH2_OK);
    assert(sh2_getProdIds(&product_ids) == SH2_ERR_TIMEOUT);
    assert(test_writes == 1U);
    assert(test_reads >= 50U && test_reads < 60U);
    sh2_close();
    puts("CEVA product-ID timeout bound PASS");
    return 0;
}
