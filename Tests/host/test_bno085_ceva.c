/**
 * @file test_bno085_ceva.c
 * @brief End-to-end host regression for the Atlas BNO085 HAL and actual CEVA stack.
 *
 * Major functions:
 * - test_queue_reset()/test_queue_product_ids(): create valid SHTP device traffic.
 * - test_receive(): enforces separate header and continuation I2C transactions.
 * - test_transmit(): validates product-ID and feature-configuration requests.
 * - main(): proves initialization and four report writes through real SH-2/SHTP.
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "atlas_bno085.h"
#include "main.h"

#define TEST_SHTP_HEADER_BYTES       (4U)
#define TEST_PRODUCT_RECORD_BYTES   (16U)
#define TEST_PRODUCT_RECORDS        (4U)
#define TEST_PRODUCT_RESPONSE       (0xF8U)
#define TEST_PRODUCT_REQUEST        (0xF9U)
#define TEST_SET_FEATURE_REQUEST    (0xFDU)
#define TEST_EXECUTABLE_CHANNEL     (1U)
#define TEST_CONTROL_CHANNEL        (2U)

static AtlasBno085 *test_sensor;
static I2C_HandleTypeDef *test_i2c;
static bool test_line_low;
static bool test_packet_queued;
static bool test_continuation_due;
static uint8_t test_channel;
static uint8_t test_sequence[8];
static uint8_t test_payload[TEST_PRODUCT_RECORD_BYTES * TEST_PRODUCT_RECORDS];
static uint16_t test_payload_length;
static uint32_t test_receive_calls;
static uint32_t test_product_requests;
static uint32_t test_feature_requests;

/** @brief Store one 32-bit little-endian test value without alignment assumptions. */
static void test_put_u32le(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

/** @brief Queue one logical payload for CEVA's header/continuation receive path. */
static void test_queue_packet(uint8_t channel, const uint8_t *payload, uint16_t length)
{
    assert(!test_packet_queued);
    assert(channel < (uint8_t)(sizeof(test_sequence) / sizeof(test_sequence[0])));
    assert(payload != NULL);
    assert(length <= sizeof(test_payload));
    memcpy(test_payload, payload, length);
    test_payload_length = length;
    test_channel = channel;
    test_continuation_due = false;
    test_packet_queued = true;
    test_line_low = true;
}

/** @brief Queue the executable-channel reset-complete response emitted at boot. */
static void test_queue_reset(void)
{
    const uint8_t reset_complete = 1U;
    test_queue_packet(TEST_EXECUTABLE_CHANNEL, &reset_complete, 1U);
}

/** @brief Queue four real-layout product records so SH-2 can complete discovery. */
static void test_queue_product_ids(void)
{
    uint8_t records[TEST_PRODUCT_RECORD_BYTES * TEST_PRODUCT_RECORDS] = {0U};
    uint32_t index;

    for (index = 0U; index < TEST_PRODUCT_RECORDS; ++index)
    {
        uint8_t *record = &records[index * TEST_PRODUCT_RECORD_BYTES];
        record[0] = TEST_PRODUCT_RESPONSE;
        record[2] = 1U;
        record[3] = (uint8_t)index;
        /* Avoid FSP200 part numbers, whose CEVA contract expects five records. */
        test_put_u32le(&record[4], 0x00123000U + index);
        test_put_u32le(&record[8], 0x01020000U + index);
        record[12] = (uint8_t)index;
    }
    test_queue_packet(TEST_CONTROL_CHANNEL, records, sizeof(records));
}

/** @brief Model H_INTN and otherwise return inactive-high inputs. */
static GPIO_PinState test_gpio_read(GPIO_TypeDef *port, uint16_t pin)
{
    if ((port == BNO085_H_INTN_GPIO_Port) && (pin == BNO085_H_INTN_Pin))
    {
        return test_line_low ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    return GPIO_PIN_SET;
}

/** @brief Queue boot traffic exactly when the project releases U12 from reset. */
static void test_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if ((port != BNO085_NRST_GPIO_Port) || (pin != BNO085_NRST_Pin))
    {
        return;
    }
    if (state == GPIO_PIN_RESET)
    {
        test_line_low = false;
        test_packet_queued = false;
        test_continuation_due = false;
    }
    else
    {
        test_queue_reset();
    }
}

/**
 * @brief Validate outbound SHTP and make its requested response interrupt-ready.
 * @return HAL_OK for the product-ID request and valid set-feature packets.
 */
static HAL_StatusTypeDef test_transmit(I2C_HandleTypeDef *i2c,
                                       uint16_t address,
                                       uint8_t *data,
                                       uint16_t length,
                                       uint32_t timeout_ms)
{
    assert(i2c == test_i2c);
    assert(address == 0x94U);
    assert(data != NULL);
    assert(length >= 5U);
    assert(timeout_ms >= 50U);
    assert(data[2] == TEST_CONTROL_CHANNEL);

    if (data[4] == TEST_PRODUCT_REQUEST)
    {
        ++test_product_requests;
        test_queue_product_ids();
        AtlasBno085_OnInterrupt(test_sensor);
        return HAL_OK;
    }
    if (data[4] == TEST_SET_FEATURE_REQUEST)
    {
        ++test_feature_requests;
        return HAL_OK;
    }
    return HAL_ERROR;
}

/**
 * @brief Return each virtual packet as header, new interrupt, then continuation.
 * @return HAL_OK only for the exact CEVA I2C lengths and shifted address.
 */
static HAL_StatusTypeDef test_receive(I2C_HandleTypeDef *i2c,
                                      uint16_t address,
                                      uint8_t *data,
                                      uint16_t length,
                                      uint32_t timeout_ms)
{
    const uint16_t transfer_length =
        (uint16_t)(test_payload_length + TEST_SHTP_HEADER_BYTES);

    assert(i2c == test_i2c);
    assert(address == 0x94U);
    assert(data != NULL);
    assert(test_packet_queued);
    assert(timeout_ms >= 50U);
    ++test_receive_calls;

    if (!test_continuation_due)
    {
        assert(length == TEST_SHTP_HEADER_BYTES);
        data[0] = (uint8_t)transfer_length;
        data[1] = (uint8_t)(transfer_length >> 8);
        data[2] = test_channel;
        data[3] = test_sequence[test_channel];
        test_continuation_due = true;

        /* The data sheet permits a very short HIGH interval. The falling-edge
         * callback, not a sampled LOW tail, authorizes the continuation. */
        test_line_low = false;
        test_line_low = true;
        AtlasBno085_OnInterrupt(test_sensor);
        return HAL_OK;
    }

    assert(length == transfer_length);
    data[0] = (uint8_t)transfer_length;
    data[1] = (uint8_t)((transfer_length >> 8) | 0x80U);
    data[2] = test_channel;
    data[3] = (uint8_t)(test_sequence[test_channel] + 1U);
    memcpy(&data[TEST_SHTP_HEADER_BYTES], test_payload, test_payload_length);
    test_sequence[test_channel] = (uint8_t)(test_sequence[test_channel] + 2U);
    test_line_low = false;
    test_packet_queued = false;
    test_continuation_due = false;
    return HAL_OK;
}

/** @brief Exercise the linked Atlas adapter and CEVA implementation end to end. */
int main(void)
{
    AtlasBno085 sensor;
    I2C_HandleTypeDef i2c = {0};
    TIM_TypeDef timer_instance = {0};
    TIM_HandleTypeDef timer = {0};

    timer.Instance = &timer_instance;
    timer.State = HAL_TIM_STATE_READY;
    test_sensor = &sensor;
    test_i2c = &i2c;
    AtlasTest_ResetGpioTrace();
    AtlasTest_ResetI2cTrace();
    AtlasTest_SetGpioReadHook(test_gpio_read);
    AtlasTest_SetGpioWriteHook(test_gpio_write);
    AtlasTest_SetI2cHooks(test_transmit, test_receive);

    assert(AtlasBno085_Init(&sensor, &i2c, &timer, NULL, NULL) == ATLAS_OK);
    assert(sensor.product_ids.numEntries == TEST_PRODUCT_RECORDS);
    assert(sensor.product_ids.entry[0].swPartNumber == 0x00123000U);
    assert(sensor.health.async_resets == 1U);
    assert(sensor.health.transfers_read == 2U);
    assert(sensor.health.transfers_written == 1U);
    assert(test_receive_calls == 4U);
    assert(test_product_requests == 1U);

    assert(AtlasBno085_EnableReport(&sensor, SH2_ACCELEROMETER, 10000U, 0U) == ATLAS_OK);
    assert(AtlasBno085_EnableReport(&sensor, SH2_GYROSCOPE_CALIBRATED, 10000U, 0U) == ATLAS_OK);
    assert(AtlasBno085_EnableReport(&sensor, SH2_MAGNETIC_FIELD_CALIBRATED, 20000U, 0U) == ATLAS_OK);
    assert(AtlasBno085_EnableReport(&sensor, SH2_ROTATION_VECTOR, 10000U, 0U) == ATLAS_OK);
    assert(test_feature_requests == 4U);
    assert(sensor.health.transfers_written == 5U);

    AtlasBno085_Deinit(&sensor);
    assert(!sensor.initialized && !sensor.session_open);
    AtlasTest_SetI2cHooks(NULL, NULL);
    AtlasTest_SetGpioWriteHook(NULL);
    AtlasTest_SetGpioReadHook(NULL);
    puts("Atlas BNO085 + actual CEVA integration PASS");
    return 0;
}
