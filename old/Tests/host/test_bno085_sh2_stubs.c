/**
 * @file test_bno085_sh2_stubs.c
 * @brief Virtual BNO085 plus minimal CEVA calls for host transport regression tests.
 *
 * Major functions:
 * - sh2_getProdIds(): drives header/idle/full/idle reads through the private HAL.
 * - sh2_setSensorConfig(): drives one bounded write through the private HAL.
 * - AtlasTest_BnoSh2Begin()/End(): isolate the virtual device from other tests.
 */

#include "test_bno085_sh2_stubs.h"

#include <string.h>

#include "atlas_bno085.h"
#include "sh2_err.h"

#define ATLAS_TEST_BNO_TRANSFER_LENGTH (SH2_HAL_MAX_TRANSFER_IN)

static sh2_Hal_t *atlas_test_hal;
static I2C_HandleTypeDef *atlas_test_expected_i2c;
static sh2_SensorCallback_t *atlas_test_sensor_callback;
static void *atlas_test_sensor_cookie;
static bool atlas_test_line_low;
static bool atlas_test_fail_header;
static bool atlas_test_contract_passed;
static uint32_t atlas_test_receive_calls;
static uint32_t atlas_test_write_calls;
static uint32_t atlas_test_full_timeout_ms;

/** @brief Model H_INTN while leaving every unrelated input inactive-high. */
static GPIO_PinState atlas_test_bno_gpio_read(GPIO_TypeDef *port, uint16_t pin)
{
    if ((port == BNO085_H_INTN_GPIO_Port) && (pin == BNO085_H_INTN_Pin))
    {
        return atlas_test_line_low ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    return GPIO_PIN_SET;
}

/** @brief Validate a project write reaches U12 at the shifted 0x94 HAL address. */
static HAL_StatusTypeDef atlas_test_bno_transmit(I2C_HandleTypeDef *i2c,
                                                 uint16_t address,
                                                 uint8_t *data,
                                                 uint16_t length,
                                                 uint32_t timeout_ms)
{
    if ((i2c != atlas_test_expected_i2c) ||
        (address != (ATLAS_BNO085_I2C_ADDRESS_7BIT << 1U)) ||
        (data == NULL) || (length == 0U) || (timeout_ms < 50U))
    {
        atlas_test_contract_passed = false;
        return HAL_ERROR;
    }
    ++atlas_test_write_calls;
    return HAL_OK;
}

/**
 * @brief Supply the initial four-byte length fragment and later full continuation.
 * @note H_INTN deliberately remains low after each receive so the test catches
 *       any code that mistakes the tail of the consumed assertion for a new edge.
 */
static HAL_StatusTypeDef atlas_test_bno_receive(I2C_HandleTypeDef *i2c,
                                                uint16_t address,
                                                uint8_t *data,
                                                uint16_t length,
                                                uint32_t timeout_ms)
{
    if ((i2c != atlas_test_expected_i2c) ||
        (address != (ATLAS_BNO085_I2C_ADDRESS_7BIT << 1U)) || (data == NULL))
    {
        atlas_test_contract_passed = false;
        return HAL_ERROR;
    }
    ++atlas_test_receive_calls;
    if (atlas_test_fail_header)
    {
        i2c->ErrorCode = 0x20U;
        return HAL_TIMEOUT;
    }
    if ((atlas_test_receive_calls == 1U) && (length == 4U))
    {
        data[0] = (uint8_t)ATLAS_TEST_BNO_TRANSFER_LENGTH;
        data[1] = (uint8_t)(ATLAS_TEST_BNO_TRANSFER_LENGTH >> 8);
        data[2] = 0U;
        data[3] = 0U;
        return HAL_OK;
    }
    if ((atlas_test_receive_calls == 2U) &&
        (length == ATLAS_TEST_BNO_TRANSFER_LENGTH))
    {
        memset(data, 0, length);
        data[0] = (uint8_t)ATLAS_TEST_BNO_TRANSFER_LENGTH;
        data[1] = (uint8_t)((ATLAS_TEST_BNO_TRANSFER_LENGTH >> 8) | 0x80U);
        data[2] = 0U;
        data[3] = 1U;
        atlas_test_full_timeout_ms = timeout_ms;
        return HAL_OK;
    }
    atlas_test_contract_passed = false;
    return HAL_ERROR;
}

void AtlasTest_BnoSh2Begin(I2C_HandleTypeDef *i2c, bool fail_header)
{
    atlas_test_hal = NULL;
    atlas_test_expected_i2c = i2c;
    atlas_test_sensor_callback = NULL;
    atlas_test_sensor_cookie = NULL;
    atlas_test_line_low = true;
    atlas_test_fail_header = fail_header;
    atlas_test_contract_passed = true;
    atlas_test_receive_calls = 0U;
    atlas_test_write_calls = 0U;
    atlas_test_full_timeout_ms = 0U;
    if (i2c != NULL)
    {
        i2c->ErrorCode = 0U;
    }
    AtlasTest_SetGpioReadHook(atlas_test_bno_gpio_read);
    AtlasTest_SetI2cHooks(atlas_test_bno_transmit, atlas_test_bno_receive);
}

bool AtlasTest_BnoSh2ContractPassed(void) { return atlas_test_contract_passed; }
uint32_t AtlasTest_BnoSh2ReceiveCalls(void) { return atlas_test_receive_calls; }
uint32_t AtlasTest_BnoSh2FullReadTimeoutMs(void) { return atlas_test_full_timeout_ms; }
uint32_t AtlasTest_BnoSh2WriteCalls(void) { return atlas_test_write_calls; }

void AtlasTest_BnoSh2End(void)
{
    atlas_test_hal = NULL;
    atlas_test_expected_i2c = NULL;
    AtlasTest_SetGpioReadHook(NULL);
    AtlasTest_SetI2cHooks(NULL, NULL);
}

int sh2_open(sh2_Hal_t *hal, sh2_EventCallback_t *event_callback, void *event_cookie)
{
    (void)event_callback;
    (void)event_cookie;
    if ((hal == NULL) || (hal->open == NULL))
    {
        return SH2_ERR_BAD_PARAM;
    }
    atlas_test_hal = hal;
    return hal->open(hal);
}

void sh2_close(void)
{
    if ((atlas_test_hal != NULL) && (atlas_test_hal->close != NULL))
    {
        atlas_test_hal->close(atlas_test_hal);
    }
    atlas_test_hal = NULL;
}

int sh2_setSensorCallback(sh2_SensorCallback_t *callback, void *cookie)
{
    atlas_test_sensor_callback = callback;
    atlas_test_sensor_cookie = cookie;
    return SH2_OK;
}

int sh2_getProdIds(sh2_ProductIds_t *product_ids)
{
    uint8_t buffer[SH2_HAL_MAX_TRANSFER_IN];
    uint32_t timestamp_us = 0U;
    uint32_t receive_calls;
    int length;

    if ((atlas_test_hal == NULL) || (product_ids == NULL))
    {
        return SH2_ERR_BAD_PARAM;
    }
    length = atlas_test_hal->read(atlas_test_hal, buffer, sizeof(buffer), &timestamp_us);
    if (atlas_test_fail_header)
    {
        return (length == SH2_ERR_IO) ? SH2_ERR_IO : SH2_ERR;
    }
    atlas_test_contract_passed &=
        (length == 4) && (buffer[0] == 0U) && (buffer[1] == 4U);

    /* The old assertion is deliberately still LOW. It must not authorize the
     * full continuation or touch I2C a second time. */
    receive_calls = atlas_test_receive_calls;
    length = atlas_test_hal->read(atlas_test_hal, buffer, sizeof(buffer), &timestamp_us);
    atlas_test_contract_passed &=
        (length == 0) && (atlas_test_receive_calls == receive_calls);

    /* Observe the required deassertion, then model the payload-ready edge. */
    atlas_test_line_low = false;
    length = atlas_test_hal->read(atlas_test_hal, buffer, sizeof(buffer), &timestamp_us);
    atlas_test_contract_passed &=
        (length == 0) && (atlas_test_receive_calls == receive_calls);
    atlas_test_line_low = true;
    AtlasBno085_OnInterrupt((AtlasBno085 *)atlas_test_hal);
    length = atlas_test_hal->read(atlas_test_hal, buffer, sizeof(buffer), &timestamp_us);
    atlas_test_contract_passed &=
        (length == ATLAS_TEST_BNO_TRANSFER_LENGTH) &&
        (buffer[0] == 0U) && (buffer[1] == 0x84U);

    /* Even while the consumed payload assertion is still LOW, a phantom third
     * transaction is illegal. */
    receive_calls = atlas_test_receive_calls;
    length = atlas_test_hal->read(atlas_test_hal, buffer, sizeof(buffer), &timestamp_us);
    atlas_test_contract_passed &=
        (length == 0) && (atlas_test_receive_calls == receive_calls);
    if (!atlas_test_contract_passed)
    {
        return SH2_ERR_IO;
    }
    memset(product_ids, 0, sizeof(*product_ids));
    product_ids->numEntries = 1U;
    product_ids->entry[0].swPartNumber = 10004095U;
    return SH2_OK;
}

int sh2_setSensorConfig(sh2_SensorId_t sensor_id, const sh2_SensorConfig_t *config)
{
    uint8_t request[8] = {0U};
    int written;

    if ((atlas_test_hal == NULL) || (config == NULL) ||
        (sensor_id > SH2_MAX_SENSOR_ID))
    {
        return SH2_ERR_BAD_PARAM;
    }
    request[0] = (uint8_t)sensor_id;
    written = atlas_test_hal->write(atlas_test_hal, request, sizeof(request));
    return (written == (int)sizeof(request)) ? SH2_OK : SH2_ERR_IO;
}

void sh2_service(void)
{
    /* The initialization contract is driven synchronously by sh2_getProdIds(). */
}

int sh2_decodeSensorEvent(sh2_SensorValue_t *value, const sh2_SensorEvent_t *event)
{
    (void)value;
    (void)event;
    return SH2_ERR;
}
