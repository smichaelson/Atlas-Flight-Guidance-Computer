/**
 * @file test_protocols.c
 * @brief Host regression tests for sensor math and GNSS frame integrity/formatting.
 *
 * Major functions:
 * - test_adxl375_register_contract(): checks identity/configuration/burst sampling.
 * - test_lsm6dsv16b_register_contract(): checks encodings and Z/Y/X accel mapping.
 * - test_ms5611_reference_vector(): checks the manufacturer compensation example.
 * - test_mmc5983_register_contract(): checks initialization and packed measurement I/O.
 * - test_gnss_nav_pvt(): checks UBX checksum validation and field offsets.
 * - test_gnss_configuration_readback(): checks ACK correlation and every RAM key.
 * - test_gnss_pps_wrap(): checks 32-bit timer wrap handling.
 * - test_ble_persistent_profile(): checks post-restart profile/DSR verification.
 * - test_ble_response_overflow(): checks that truncated AT evidence fails closed.
 * - test_rfd_parameter_readback(): checks identity/settings and S-register verification.
 * - test_bno085_i2c_contract(): checks staged SHTP reads and shared-bus recovery.
 * - test_buzzer_differential_contract(): checks opposite PWM modes and timed stop.
 * - test_led_gpio_ownership(): checks TIM4-staged pins remain hard-inhibited low.
 * - test_uart_overflow_accounting(): checks fail-visible SPSC ring overflow.
 * - test_rtos_period_policy(): checks wrap-safe periodic scheduling.
 * - test_rtos_supervisor_policy(): checks liveness, maintenance, status, and stack gates.
 * - main(): runs every deterministic host test and reports failures.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atlas_adxl375.h"
#include "atlas_ble.h"
#include "atlas_bno085.h"
#include "atlas_buzzer.h"
#include "atlas_gnss.h"
#include "atlas_led.h"
#include "atlas_lsm6dsv16b.h"
#include "atlas_mmc5983ma.h"
#include "atlas_ms5611.h"
#include "atlas_rfd900x.h"
#include "atlas_rtos_policy.h"
#include "test_bno085_sh2_stubs.h"

/* U12 SA0/H_MOSI is hard-strapped low on Atlas rev-0.1. Keep the transport
 * address tied to that immutable board contract, not a remembered default. */
_Static_assert(ATLAS_BNO085_I2C_ADDRESS_7BIT == 0x4AU,
               "Atlas rev-0.1 BNO085 address must follow the SA0-low strap");

static unsigned atlas_test_failures;
static AtlasUartTransport *atlas_test_gnss_transport;
static bool atlas_test_gnss_readback_mismatch;
static AtlasUartTransport *atlas_test_ble_transport;
static bool atlas_test_ble_bad_readback;
static unsigned atlas_test_ble_profile_queries;
static unsigned atlas_test_ble_post_restart_queries;
static unsigned atlas_test_ble_stores;
static unsigned atlas_test_ble_poweroffs;
static bool atlas_test_ble_poweroff_seen;
static bool atlas_test_ble_transition_reply_enabled;
static const char *atlas_test_ble_expected_name = "Atlas-FGC";
static bool atlas_test_ble_name_quoted = true;
static char atlas_test_ble_last_name_command[128];
static AtlasUartTransport *atlas_test_rfd_transport;
static bool atlas_test_rfd_bad_readback;
static bool atlas_test_rfd_empty_identity;
static unsigned atlas_test_rfd_stores;
static char atlas_test_rfd_last_write[64];
static SPI_HandleTypeDef atlas_test_adxl_spi;
static SPI_HandleTypeDef atlas_test_lsm_spi;
static SPI_HandleTypeDef atlas_test_mmc_spi;
static uint8_t atlas_test_adxl_registers[64];
static uint8_t atlas_test_lsm_registers[128];
static uint8_t atlas_test_mmc_registers[64];
static I2C_HandleTypeDef atlas_test_ms5611_i2c;
static uint16_t atlas_test_ms5611_prom[8];
static uint8_t atlas_test_ms5611_command;
static uint8_t atlas_test_ms5611_conversion;
static unsigned atlas_test_ms5611_resets;
static unsigned atlas_test_ms5611_prom_reads;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++atlas_test_failures; \
    } \
} while (0)

/**
 * @brief Emulate the three direct-register SPI sensors used by host tests.
 * @param spi Mock bus identity.
 * @param tx Transmitted frame.
 * @param rx Receive frame to populate.
 * @param length Frame length.
 * @param timeout_ms Driver timeout, accepted but not advanced.
 * @return HAL_OK for a recognized and bounded transfer.
 */
static HAL_StatusTypeDef sensor_spi_transfer(SPI_HandleTypeDef *spi,
                                             uint8_t *tx,
                                             uint8_t *rx,
                                             uint16_t length,
                                             uint32_t timeout_ms)
{
    uint8_t *registers;
    uint8_t register_mask;
    uint8_t address;
    uint16_t index;
    bool read;

    (void)timeout_ms;
    if ((tx == NULL) || (rx == NULL) || (length < 2U))
    {
        return HAL_ERROR;
    }
    if (spi == &atlas_test_adxl_spi)
    {
        registers = atlas_test_adxl_registers;
        register_mask = 0x3FU;
    }
    else if (spi == &atlas_test_lsm_spi)
    {
        registers = atlas_test_lsm_registers;
        register_mask = 0x7FU;
    }
    else if (spi == &atlas_test_mmc_spi)
    {
        registers = atlas_test_mmc_registers;
        register_mask = 0x3FU;
    }
    else
    {
        return HAL_ERROR;
    }

    memset(rx, 0, length);
    read = (tx[0] & 0x80U) != 0U;
    address = tx[0] & register_mask;
    if (read)
    {
        for (index = 1U; index < length; ++index)
        {
            rx[index] = registers[(uint8_t)(address + index - 1U)];
        }
        return HAL_OK;
    }
    if (length != 2U)
    {
        return HAL_ERROR;
    }
    registers[address] = tx[1];
    if ((spi == &atlas_test_lsm_spi) && (address == 0x12U) &&
        ((tx[1] & 0x01U) != 0U))
    {
        /* Model completion of the self-clearing software-reset bit. */
        registers[address] = 0U;
    }
    if ((spi == &atlas_test_mmc_spi) && (address == 0x0AU) &&
        ((tx[1] & 0x80U) != 0U))
    {
        const uint8_t product_id = registers[0x2FU];
        memset(registers, 0, sizeof(atlas_test_mmc_registers));
        registers[0x2FU] = product_id;
    }
    if ((spi == &atlas_test_mmc_spi) && (address == 0x09U))
    {
        if ((tx[1] & 0x01U) != 0U) { registers[0x08U] |= 0x01U; }
        if ((tx[1] & 0x02U) != 0U) { registers[0x08U] |= 0x02U; }
    }
    return HAL_OK;
}

/** @brief Verify ADXL375 register encodings, readbacks, and coherent sample order. */
static void test_adxl375_register_contract(void)
{
    AtlasAdxl375 sensor;
    AtlasAdxl375Sample sample;
    bool ready = false;

    memset(&atlas_test_adxl_spi, 0, sizeof(atlas_test_adxl_spi));
    memset(atlas_test_adxl_registers, 0, sizeof(atlas_test_adxl_registers));
    atlas_test_adxl_registers[0x00U] = ATLAS_ADXL375_DEVICE_ID;
    AtlasTest_SetSpiTransferHook(sensor_spi_transfer);
    CHECK(AtlasAdxl375_Init(&sensor, &atlas_test_adxl_spi,
                            &atlas_test_gpio_g, GPIO_PIN_5,
                            ATLAS_ADXL375_ODR_400_HZ) == ATLAS_OK);
    CHECK(atlas_test_adxl_registers[0x2CU] == 0x0CU);
    CHECK(atlas_test_adxl_registers[0x2DU] == 0x08U);
    CHECK(atlas_test_adxl_registers[0x2EU] == 0x00U);
    CHECK(atlas_test_adxl_registers[0x31U] == 0x0BU);

    atlas_test_adxl_registers[0x30U] = 0x80U;
    atlas_test_adxl_registers[0x32U] = 0x34U;
    atlas_test_adxl_registers[0x33U] = 0x12U;
    atlas_test_adxl_registers[0x34U] = 0xFEU;
    atlas_test_adxl_registers[0x35U] = 0xFFU;
    atlas_test_adxl_registers[0x36U] = 0x00U;
    atlas_test_adxl_registers[0x37U] = 0x80U;
    CHECK(AtlasAdxl375_DataReady(&sensor, &ready) == ATLAS_OK);
    CHECK(ready);
    CHECK(AtlasAdxl375_ReadSample(&sensor, &sample) == ATLAS_OK);
    CHECK(sample.raw_x == 0x1234);
    CHECK(sample.raw_y == -2);
    CHECK(sample.raw_z == INT16_MIN);
    CHECK((sample.x_g > 228.32F) && (sample.x_g < 228.35F));
}

/** @brief Verify LSM6DSV16B ODR/FS encodings and the documented Z/Y/X accel map. */
static void test_lsm6dsv16b_register_contract(void)
{
    AtlasLsm6dsv16b sensor;
    AtlasLsm6dsv16bSample sample;
    AtlasLsm6dsv16bConfig config = AtlasLsm6dsv16b_DefaultConfig();

    memset(&atlas_test_lsm_spi, 0, sizeof(atlas_test_lsm_spi));
    memset(atlas_test_lsm_registers, 0, sizeof(atlas_test_lsm_registers));
    atlas_test_lsm_registers[0x0FU] = ATLAS_LSM6DSV16B_WHO_AM_I;
    AtlasTest_SetSpiTransferHook(sensor_spi_transfer);
    CHECK(AtlasLsm6dsv16b_Init(&sensor, &atlas_test_lsm_spi,
                               &atlas_test_gpio_g, GPIO_PIN_5,
                               &config) == ATLAS_OK);
    CHECK(atlas_test_lsm_registers[0x10U] == 0x07U);
    CHECK(atlas_test_lsm_registers[0x11U] == 0x07U);
    CHECK(atlas_test_lsm_registers[0x12U] == 0x44U);
    CHECK(atlas_test_lsm_registers[0x15U] == 0x04U);
    CHECK(atlas_test_lsm_registers[0x17U] == 0x03U);
    CHECK(atlas_test_lsm_registers[0x0DU] == 0x03U);

    atlas_test_lsm_registers[0x1EU] = 0x03U;
    atlas_test_lsm_registers[0x20U] = 0x00U; /* temperature = 256 -> 26 C */
    atlas_test_lsm_registers[0x21U] = 0x01U;
    atlas_test_lsm_registers[0x22U] = 1U;    /* gyro X = 1 */
    atlas_test_lsm_registers[0x24U] = 2U;    /* gyro Y = 2 */
    atlas_test_lsm_registers[0x26U] = 3U;    /* gyro Z = 3 */
    atlas_test_lsm_registers[0x28U] = 4U;    /* accel Z = 4 */
    atlas_test_lsm_registers[0x2AU] = 5U;    /* accel Y = 5 */
    atlas_test_lsm_registers[0x2CU] = 6U;    /* accel X = 6 */
    CHECK(AtlasLsm6dsv16b_ReadSample(&sensor, &sample) == ATLAS_OK);
    CHECK(sample.raw_gyro_x == 1);
    CHECK(sample.raw_gyro_y == 2);
    CHECK(sample.raw_gyro_z == 3);
    CHECK(sample.raw_accel_x == 6);
    CHECK(sample.raw_accel_y == 5);
    CHECK(sample.raw_accel_z == 4);
    CHECK(sample.temperature_c == 26.0F);
}

/** @brief Emulate MS5611 command writes and retain conversion selection. */
static HAL_StatusTypeDef ms5611_i2c_transmit(I2C_HandleTypeDef *i2c,
                                             uint16_t address,
                                             uint8_t *data,
                                             uint16_t length,
                                             uint32_t timeout_ms)
{
    (void)timeout_ms;
    if ((i2c != &atlas_test_ms5611_i2c) || (address != (0x77U << 1)) ||
        (data == NULL) || (length != 1U))
    {
        return HAL_ERROR;
    }
    atlas_test_ms5611_command = data[0];
    if (data[0] == 0x1EU) { ++atlas_test_ms5611_resets; }
    if ((data[0] & 0xF0U) == 0x40U) { atlas_test_ms5611_conversion = 1U; }
    if ((data[0] & 0xF0U) == 0x50U) { atlas_test_ms5611_conversion = 2U; }
    return HAL_OK;
}

/** @brief Emulate MS5611 PROM and 24-bit ADC reads. */
static HAL_StatusTypeDef ms5611_i2c_receive(I2C_HandleTypeDef *i2c,
                                            uint16_t address,
                                            uint8_t *data,
                                            uint16_t length,
                                            uint32_t timeout_ms)
{
    (void)timeout_ms;
    if ((i2c != &atlas_test_ms5611_i2c) || (address != (0x77U << 1)) ||
        (data == NULL))
    {
        return HAL_ERROR;
    }
    if ((atlas_test_ms5611_command >= 0xA0U) &&
        (atlas_test_ms5611_command <= 0xAEU) &&
        ((atlas_test_ms5611_command & 1U) == 0U) && (length == 2U))
    {
        const uint8_t index = (uint8_t)((atlas_test_ms5611_command - 0xA0U) / 2U);
        data[0] = (uint8_t)(atlas_test_ms5611_prom[index] >> 8);
        data[1] = (uint8_t)atlas_test_ms5611_prom[index];
        ++atlas_test_ms5611_prom_reads;
        return HAL_OK;
    }
    if ((atlas_test_ms5611_command == 0x00U) && (length == 3U))
    {
        const uint32_t raw = (atlas_test_ms5611_conversion == 1U) ?
                             9085466UL : 8569150UL;
        data[0] = (uint8_t)(raw >> 16);
        data[1] = (uint8_t)(raw >> 8);
        data[2] = (uint8_t)raw;
        return HAL_OK;
    }
    return HAL_ERROR;
}

/** @brief Model all three gate nets held high during fail-dark initialization. */
static GPIO_PinState led_gate_stuck_high(GPIO_TypeDef *port, uint16_t pin)
{
    (void)port;
    (void)pin;
    return GPIO_PIN_SET;
}

/** @brief Prove LED GPIO ownership, fail-dark initialization, and hard inhibit. */
static void test_led_gpio_ownership(void)
{
    AtlasLed led;

    AtlasTest_SetGpioReadHook(led_gate_stuck_high);
    CHECK(AtlasLed_Init(&led) == ATLAS_ERROR_IO);
    CHECK(!led.initialized);
    CHECK(AtlasLed_ReadGateMask(&led) == ATLAS_LED_WHITE);
    AtlasTest_SetGpioReadHook(NULL);

    AtlasTest_ResetGpioTrace();
    CHECK(AtlasLed_Init(&led) == ATLAS_OK);
    CHECK((AtlasTest_GetOutputPins(LED_R_GPIO_Port) &
           (LED_R_Pin | LED_G_Pin)) == (LED_R_Pin | LED_G_Pin));
    CHECK((AtlasTest_GetOutputPins(LED_B_GPIO_Port) & LED_B_Pin) == LED_B_Pin);
    CHECK((AtlasTest_GetHighPins(LED_R_GPIO_Port) &
           (LED_R_Pin | LED_G_Pin)) == 0U);
    CHECK((AtlasTest_GetHighPins(LED_B_GPIO_Port) & LED_B_Pin) == 0U);

    CHECK(led.output_inhibited);
    CHECK(AtlasLed_SetColor(&led, ATLAS_LED_MAGENTA) == ATLAS_ERROR_UNSUPPORTED);
    CHECK((AtlasTest_GetHighPins(LED_R_GPIO_Port) & LED_R_Pin) == 0U);
    CHECK((AtlasTest_GetHighPins(LED_R_GPIO_Port) & LED_G_Pin) == 0U);
    CHECK((AtlasTest_GetHighPins(LED_B_GPIO_Port) & LED_B_Pin) == 0U);
    CHECK(AtlasLed_ReadGateMask(&led) == ATLAS_LED_OFF);
    CHECK(led.color == ATLAS_LED_OFF);
    CHECK(AtlasLed_SetRgb(&led, true, true, true) == ATLAS_ERROR_UNSUPPORTED);
    CHECK(AtlasLed_ReadGateMask(&led) == ATLAS_LED_OFF);
    AtlasTest_SetGpioReadHook(led_gate_stuck_high);
    CHECK(AtlasLed_SetColor(&led, ATLAS_LED_OFF) == ATLAS_ERROR_IO);
    AtlasTest_SetGpioReadHook(NULL);
    AtlasLed_Off(&led);
    CHECK(AtlasLed_ReadGateMask(&led) == ATLAS_LED_OFF);
}

/**
 * @brief Verify CEVA's two-interrupt I2C framing and recovery of the shared bus.
 */
static void test_bno085_i2c_contract(void)
{
    AtlasBno085 sensor;
    I2C_HandleTypeDef i2c;
    TIM_TypeDef timer_instance;
    TIM_HandleTypeDef timer;

    memset(&i2c, 0, sizeof(i2c));
    memset(&timer_instance, 0, sizeof(timer_instance));
    memset(&timer, 0, sizeof(timer));
    timer.Instance = &timer_instance;
    timer.State = HAL_TIM_STATE_READY;
    AtlasTest_ResetI2cTrace();
    AtlasTest_BnoSh2Begin(&i2c, false);
    CHECK(AtlasBno085_Init(&sensor, &i2c, &timer, NULL, NULL) == ATLAS_OK);
    CHECK(sensor.initialized && sensor.session_open && !sensor.transport_failed);
    CHECK(sensor.address_hal == 0x94U);
    CHECK(sensor.pending_transfer_length == 0U);
    CHECK(sensor.health.transfers_read == 1U);
    CHECK(sensor.health.io_errors == 0U);
    CHECK(AtlasTest_BnoSh2ReceiveCalls() == 2U);
    CHECK(AtlasTest_BnoSh2ContractPassed());
    CHECK(AtlasTest_BnoSh2FullReadTimeoutMs() >= 143U);
    CHECK(AtlasTest_BnoSh2FullReadTimeoutMs() < 250U);
    CHECK(AtlasBno085_EnableReport(&sensor, SH2_ACCELEROMETER, 10000U, 0U) == ATLAS_OK);
    CHECK(AtlasTest_BnoSh2WriteCalls() == 1U);
    AtlasBno085_Deinit(&sensor);
    CHECK(!sensor.initialized && !sensor.session_open);
    CHECK(AtlasTest_GetI2cDeinitCount() == 0U);
    AtlasTest_BnoSh2End();

    memset(&i2c, 0, sizeof(i2c));
    memset(&timer_instance, 0, sizeof(timer_instance));
    memset(&timer, 0, sizeof(timer));
    timer.Instance = &timer_instance;
    timer.State = HAL_TIM_STATE_READY;
    AtlasTest_ResetGpioTrace();
    AtlasTest_ResetI2cTrace();
    AtlasTest_BnoSh2Begin(&i2c, true);
    CHECK(AtlasBno085_Init(&sensor, &i2c, &timer, NULL, NULL) == ATLAS_ERROR_IO);
    CHECK(!sensor.initialized && !sensor.session_open && sensor.transport_failed);
    CHECK(AtlasTest_BnoSh2ReceiveCalls() == 3U);
    CHECK(sensor.health.io_errors == 1U);
    CHECK(sensor.health.last_hal_status == HAL_TIMEOUT);
    CHECK(sensor.health.last_hal_error == 0x20U);
    CHECK(sensor.health.last_failure_stage == ATLAS_BNO085_FAILURE_READ_HEADER);
    CHECK(sensor.health.last_transfer_length == 4U);
    CHECK(sensor.health.bus_recovery_attempts == 1U);
    CHECK(sensor.health.bus_recovery_failures == 0U);
    CHECK(AtlasTest_GetI2cDeinitCount() == 1U);
    CHECK(AtlasTest_GetI2cDeinitWhileBnoResetCount() == 1U);
    CHECK(AtlasTest_GetI2cInitCount() == 1U);
    CHECK(AtlasTest_GetI2cAnalogFilterCount() == 1U);
    CHECK(AtlasTest_GetI2cDigitalFilterCount() == 1U);
    CHECK((AtlasTest_GetHighPins(BNO085_NRST_GPIO_Port) & BNO085_NRST_Pin) == 0U);
    AtlasTest_BnoSh2End();
}

/** @brief Verify TIM15 opposite-phase configuration, bounds, start, and timed stop. */
static void test_buzzer_differential_contract(void)
{
    AtlasBuzzer buzzer;
    TIM_HandleTypeDef timer;

    memset(&timer, 0, sizeof(timer));
    timer.Instance = TIM15;
    timer.Init.Prescaler = 99U;
    AtlasTest_ResetTimerTrace();
    CHECK(AtlasBuzzer_Init(&buzzer, &timer) == ATLAS_OK);
    CHECK(timer.autoreload == 207U);
    CHECK(AtlasTest_GetTimerMode(TIM_CHANNEL_1) == TIM_OCMODE_PWM1);
    CHECK(AtlasTest_GetTimerMode(TIM_CHANNEL_2) == TIM_OCMODE_PWM2);
    CHECK(AtlasTest_GetTimerStartedMask() == 0U);
    CHECK(buzzer.frequency_hz == 4807U);

    CHECK(AtlasBuzzer_Start(&buzzer, 999U) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBuzzer_Beep(&buzzer, 4800U, 100U) == ATLAS_OK);
    CHECK(AtlasTest_GetTimerStartedMask() ==
          ((1UL << TIM_CHANNEL_1) | (1UL << TIM_CHANNEL_2)));
    HAL_Delay(99U);
    AtlasBuzzer_Service(&buzzer);
    CHECK(buzzer.running);
    HAL_Delay(1U);
    AtlasBuzzer_Service(&buzzer);
    CHECK(!buzzer.running);
    CHECK(AtlasTest_GetTimerStartedMask() == 0U);
}

/**
 * @brief Check TE Connectivity's published compensation example and an independent CRC value.
 */
static void test_ms5611_reference_vector(void)
{
    uint16_t prom[8] = {0x1234U, 40127U, 36924U, 23317U,
                        23282U, 33464U, 28312U, 0U};
    AtlasMs5611 sensor;
    AtlasMs5611 invalid_sensor;
    AtlasMs5611Sample sample;
    int32_t pressure = 0;
    int32_t temperature = 0;

    CHECK(AtlasMs5611_Crc4(prom) == 6U);
    CHECK(AtlasMs5611_Compensate(prom,
                                 9085466UL,
                                 8569150UL,
                                 &pressure,
                                 &temperature) == ATLAS_OK);
    CHECK(temperature == 2007);
    CHECK(pressure == 100009);

    prom[7] = 6U;
    memset(&atlas_test_ms5611_i2c, 0, sizeof(atlas_test_ms5611_i2c));
    memcpy(atlas_test_ms5611_prom, prom, sizeof(prom));
    atlas_test_ms5611_resets = 0U;
    atlas_test_ms5611_prom_reads = 0U;
    atlas_test_ms5611_command = 0U;
    atlas_test_ms5611_conversion = 0U;
    AtlasTest_SetI2cHooks(ms5611_i2c_transmit, ms5611_i2c_receive);
    CHECK(AtlasMs5611_Init(&sensor, &atlas_test_ms5611_i2c) == ATLAS_OK);
    CHECK(atlas_test_ms5611_resets == 1U);
    CHECK(atlas_test_ms5611_prom_reads == 8U);
    CHECK(AtlasMs5611_Read(&sensor, ATLAS_MS5611_OSR_4096, &sample) == ATLAS_OK);
    CHECK(sample.raw_pressure_d1 == 9085466UL);
    CHECK(sample.raw_temperature_d2 == 8569150UL);
    CHECK(sample.temperature_centi_c == 2007);
    CHECK(sample.pressure_pa == 100009);

    atlas_test_ms5611_prom[7] ^= 1U;
    CHECK(AtlasMs5611_Init(&invalid_sensor, &atlas_test_ms5611_i2c) ==
          ATLAS_ERROR_CRC);
    CHECK(invalid_sensor.health.prom_crc_failures == 1U);
    AtlasTest_SetI2cHooks(NULL, NULL);
}

/** @brief Verify MMC5983MA identity, bandwidth readback, packing, and conversion I/O. */
static void test_mmc5983_register_contract(void)
{
    const uint32_t expected_x = 0x12345UL;
    const uint32_t expected_y = 0x2ABCDUL;
    const uint32_t expected_z = 0x00003UL;
    uint8_t bytes[7] = {0U};
    AtlasMmc5983ma sensor;
    AtlasMmc5983maRaw raw;
    AtlasMmc5983maField field;
    float temperature_c = 0.0F;

    memset(&atlas_test_mmc_spi, 0, sizeof(atlas_test_mmc_spi));
    memset(atlas_test_mmc_registers, 0, sizeof(atlas_test_mmc_registers));
    atlas_test_mmc_registers[0x2FU] = ATLAS_MMC5983MA_PRODUCT_ID;
    AtlasTest_SetSpiTransferHook(sensor_spi_transfer);
    CHECK(AtlasMmc5983ma_Init(&sensor, &atlas_test_mmc_spi,
                              &atlas_test_gpio_g, GPIO_PIN_5,
                              ATLAS_MMC5983_BW_200_HZ_4_MS) == ATLAS_OK);
    CHECK((atlas_test_mmc_registers[0x0AU] & 0x03U) == 0x01U);

    bytes[0] = (uint8_t)(expected_x >> 10);
    bytes[1] = (uint8_t)(expected_x >> 2);
    bytes[2] = (uint8_t)(expected_y >> 10);
    bytes[3] = (uint8_t)(expected_y >> 2);
    bytes[4] = (uint8_t)(expected_z >> 10);
    bytes[5] = (uint8_t)(expected_z >> 2);
    bytes[6] = (uint8_t)(((expected_x & 3U) << 6) |
                         ((expected_y & 3U) << 4) |
                         ((expected_z & 3U) << 2));

    CHECK(AtlasMmc5983ma_UnpackRaw(bytes, &raw) == ATLAS_OK);
    CHECK(raw.x == expected_x);
    CHECK(raw.y == expected_y);
    CHECK(raw.z == expected_z);

    memcpy(&atlas_test_mmc_registers[0x00U], bytes, sizeof(bytes));
    CHECK(AtlasMmc5983ma_ReadField(&sensor, &field, 10U) == ATLAS_OK);
    CHECK(field.raw.x == expected_x);
    CHECK(field.raw.y == expected_y);
    CHECK(field.raw.z == expected_z);
    CHECK(sensor.health.measurements == 1U);

    atlas_test_mmc_registers[0x07U] = 125U;
    CHECK(AtlasMmc5983ma_ReadTemperature(&sensor, &temperature_c, 10U) == ATLAS_OK);
    CHECK(temperature_c == 25.0F);
    AtlasTest_SetSpiTransferHook(NULL);
}

/**
 * @brief Encode one little-endian unsigned 16-bit test field.
 * @param destination Two-byte destination.
 * @param value Value to encode.
 */
static void put_u16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Encode one little-endian unsigned 32-bit test field.
 * @param destination Four-byte destination.
 * @param value Value to encode.
 */
static void put_u32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
    destination[2] = (uint8_t)(value >> 16);
    destination[3] = (uint8_t)(value >> 24);
}

/**
 * @brief Frame one UBX payload, including its Fletcher checksum.
 * @param frame Destination with payload length plus eight bytes.
 * @param message_class UBX class.
 * @param message_id UBX identifier.
 * @param payload Payload bytes.
 * @param payload_length Payload byte count.
 * @return Complete frame length.
 */
static size_t make_ubx(uint8_t *frame,
                       uint8_t message_class,
                       uint8_t message_id,
                       const uint8_t *payload,
                       uint16_t payload_length)
{
    uint8_t checksum_a = 0U;
    uint8_t checksum_b = 0U;
    size_t index;

    frame[0] = 0xB5U;
    frame[1] = 0x62U;
    frame[2] = message_class;
    frame[3] = message_id;
    frame[4] = (uint8_t)payload_length;
    frame[5] = (uint8_t)(payload_length >> 8);
    memcpy(&frame[6], payload, payload_length);
    for (index = 2U; index < (size_t)(6U + payload_length); ++index)
    {
        checksum_a = (uint8_t)(checksum_a + frame[index]);
        checksum_b = (uint8_t)(checksum_b + checksum_a);
    }
    frame[6U + payload_length] = checksum_a;
    frame[7U + payload_length] = checksum_b;
    return (size_t)payload_length + 8U;
}

/**
 * @brief Append one little-endian configuration key followed by a U1 value.
 * @param payload Destination payload.
 * @param index Current write position, updated on return.
 * @param key Configuration key.
 * @param value One-byte value.
 */
static void append_cfg_u1(uint8_t *payload,
                          size_t *index,
                          uint32_t key,
                          uint8_t value)
{
    put_u32(&payload[*index], key);
    *index += 4U;
    payload[(*index)++] = value;
}

/**
 * @brief Append one little-endian configuration key followed by a U2 value.
 * @param payload Destination payload.
 * @param index Current write position, updated on return.
 * @param key Configuration key.
 * @param value Two-byte value.
 */
static void append_cfg_u2(uint8_t *payload,
                          size_t *index,
                          uint32_t key,
                          uint16_t value)
{
    put_u32(&payload[*index], key);
    *index += 4U;
    put_u16(&payload[*index], value);
    *index += 2U;
}

/**
 * @brief Inject bytes into the transport ring exactly as its ISR producer would.
 * @param transport Destination transport.
 * @param bytes Source bytes.
 * @param length Byte count.
 */
static void inject_uart(AtlasUartTransport *transport,
                        const uint8_t *bytes,
                        size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index)
    {
        transport->rx_ring[transport->rx_head] = bytes[index];
        transport->rx_head = (uint16_t)((transport->rx_head + 1U) &
                              (ATLAS_UART_RX_RING_CAPACITY - 1U));
    }
}

/**
 * @brief Inject one NUL-terminated AT transcript into a transport receive ring.
 * @param transport Destination transport.
 * @param text Transcript bytes excluding the NUL terminator.
 */
static void inject_text(AtlasUartTransport *transport, const char *text)
{
    inject_uart(transport, (const uint8_t *)text, strlen(text));
}

/**
 * @brief Emulate u-connectXpress writes, exact queries, restart verification, and ATO1.
 * @param uart UART used by the BLE driver.
 * @param data Wire command including CR/LF.
 * @param length Wire byte count.
 */
static void ble_command_reply(UART_HandleTypeDef *uart,
                              const uint8_t *data,
                              uint16_t length)
{
    char command[128];
    char name_reply[96];
    const char *reply = "OK\r\n";
    size_t command_length = length;

    (void)uart;
    if ((atlas_test_ble_transport == NULL) ||
        (command_length >= sizeof(command)))
    {
        return;
    }
    while ((command_length > 0U) &&
           ((data[command_length - 1U] == '\r') ||
            (data[command_length - 1U] == '\n')))
    {
        --command_length;
    }
    memcpy(command, data, command_length);
    command[command_length] = '\0';

    if (strcmp(command, "AT+UBTLN?") == 0)
    {
        ++atlas_test_ble_profile_queries;
        atlas_test_ble_post_restart_queries += atlas_test_ble_poweroff_seen ? 1U : 0U;
        (void)snprintf(name_reply, sizeof(name_reply),
            atlas_test_ble_name_quoted ? "+UBTLN:\"%s\"\r\nOK\r\n" : "+UBTLN:%s\r\nOK\r\n",
            atlas_test_ble_bad_readback ? "Wrong" : atlas_test_ble_expected_name);
        reply = name_reply;
    }
    else if (strncmp(command, "AT+UBTLN=\"", 10U) == 0)
    {
        memcpy(atlas_test_ble_last_name_command, command, command_length + 1U);
    }
    else if (strcmp(command, "AT+UBTLE?") == 0)
    {
        ++atlas_test_ble_profile_queries;
        atlas_test_ble_post_restart_queries += atlas_test_ble_poweroff_seen ? 1U : 0U;
        reply = "+UBTLE:2\r\nOK\r\n";
    }
    else if (strcmp(command, "AT+UDSC=0") == 0)
    {
        ++atlas_test_ble_profile_queries;
        atlas_test_ble_post_restart_queries += atlas_test_ble_poweroff_seen ? 1U : 0U;
        reply = "+UDSC:0,6\r\nOK\r\n";
    }
    else if (strcmp(command, "AT+UMSM?") == 0)
    {
        ++atlas_test_ble_profile_queries;
        atlas_test_ble_post_restart_queries += atlas_test_ble_poweroff_seen ? 1U : 0U;
        reply = "+UMSM:1\r\nOK\r\n";
    }
    else if (strcmp(command, "AT&W") == 0)
    {
        ++atlas_test_ble_stores;
    }
    else if (strcmp(command, "AT+CPWROFF") == 0)
    {
        ++atlas_test_ble_poweroffs;
        atlas_test_ble_poweroff_seen = true;
    }
    else if (strcmp(command, "AT+LONG") == 0)
    {
        reply = "123456789ABC\r\nOK\r\n";
    }
    inject_text(atlas_test_ble_transport, reply);
}

/**
 * @brief Emulate the unsolicited OK issued by an AT&D1 DSR low-to-high transition.
 * @param port Mock GPIO port.
 * @param pin Mock pin mask.
 * @param state New output state.
 */
static void ble_gpio_transition_reply(GPIO_TypeDef *port,
                                      uint16_t pin,
                                      GPIO_PinState state)
{
    if (atlas_test_ble_transition_reply_enabled &&
        (atlas_test_ble_transport != NULL) &&
        (port == BLE_DSR_GPIO_Port) && (pin == BLE_DSR_Pin) &&
        (state == GPIO_PIN_SET))
    {
        inject_text(atlas_test_ble_transport, "OK\r\n");
    }
}

/**
 * @brief Emulate echo-enabled SiK parameter writes, queries, and EEPROM stores.
 * @param uart UART used by the radio driver.
 * @param data Wire command including CR.
 * @param length Wire byte count.
 */
static void rfd_command_reply(UART_HandleTypeDef *uart,
                              const uint8_t *data,
                              uint16_t length)
{
    char command[64];
    char reply[96];
    size_t command_length = length;

    (void)uart;
    if ((atlas_test_rfd_transport == NULL) ||
        (command_length >= sizeof(command)))
    {
        return;
    }
    while ((command_length > 0U) &&
           ((data[command_length - 1U] == '\r') ||
            (data[command_length - 1U] == '\n')))
    {
        --command_length;
    }
    memcpy(command, data, command_length);
    command[command_length] = '\0';
    if (strcmp(command, "ATI") == 0)
    {
        (void)snprintf(reply, sizeof(reply),
                       atlas_test_rfd_empty_identity ?
                       "ATI\r\nOK\r\n" :
                       "ATI\r\nRFD900x SiK 3.57\r\nOK\r\n");
    }
    else if (strcmp(command, "ATI5") == 0)
    {
        (void)snprintf(reply, sizeof(reply),
                       "ATI5\r\nS0: 25\r\nS1: 57\r\nOK\r\n");
    }
    else if (strcmp(command, "ATS5?") == 0)
    {
        (void)snprintf(reply, sizeof(reply),
                       "ATS5?\r\nS5: %u\r\nOK\r\n",
                       atlas_test_rfd_bad_readback ? 41U : 42U);
    }
    else if (strcmp(command, "ATS0?") == 0)
    {
        (void)snprintf(reply, sizeof(reply), "ATS0?\r\nS0: 0\r\nOK\r\n");
    }
    else if (strcmp(command, "ATS255?") == 0)
    {
        (void)snprintf(reply, sizeof(reply), "ATS255?\r\nS255: 4294967295\r\nOK\r\n");
    }
    else
    {
        (void)snprintf(reply, sizeof(reply), "%s\r\nOK\r\n", command);
        if (strncmp(command, "ATS", 3U) == 0 && strchr(command, '=') != NULL)
            memcpy(atlas_test_rfd_last_write, command, command_length + 1U);
        if (strcmp(command, "AT&W") == 0)
        {
            ++atlas_test_rfd_stores;
        }
    }
    inject_text(atlas_test_rfd_transport, reply);
}

/**
 * @brief Emulate the receiver's ACK and CFG-VALGET replies for one configuration call.
 * @param uart UART used by the driver.
 * @param data Complete transmitted UBX frame.
 * @param length Frame byte count.
 */
static void gnss_configuration_reply(UART_HandleTypeDef *uart,
                                     const uint8_t *data,
                                     uint16_t length)
{
    uint8_t payload[40] = {0U};
    uint8_t frame[48];
    size_t index = 4U;
    size_t frame_length;

    (void)uart;
    if ((atlas_test_gnss_transport == NULL) || (length < 8U) ||
        (data[0] != 0xB5U) || (data[1] != 0x62U) || (data[2] != 0x06U))
    {
        return;
    }
    if (data[3] == 0x8AU)
    {
        const uint8_t ack_payload[2] = {0x06U, 0x8AU};
        frame_length = make_ubx(frame, 0x05U, 0x01U,
                                ack_payload, sizeof(ack_payload));
        inject_uart(atlas_test_gnss_transport, frame, frame_length);
    }
    else if (data[3] == 0x8BU)
    {
        /* Deliberately reverse key order to prove lookup is order-independent. */
        payload[0] = 1U; /* VALGET response version. */
        payload[1] = 0U; /* RAM layer. */
        append_cfg_u1(payload, &index, 0x20910007UL, 1U);
        append_cfg_u1(payload, &index, 0x10740002UL,
                      atlas_test_gnss_readback_mismatch ? 1U : 0U);
        append_cfg_u1(payload, &index, 0x10740001UL, 1U);
        append_cfg_u1(payload, &index, 0x20210003UL, 0U);
        append_cfg_u2(payload, &index, 0x30210002UL, 1U);
        append_cfg_u2(payload, &index, 0x30210001UL, 100U);
        frame_length = make_ubx(frame, 0x06U, 0x8BU, payload, (uint16_t)index);
        inject_uart(atlas_test_gnss_transport, frame, frame_length);
    }
}

/**
 * @brief Verify checksum rejection and every important NAV-PVT field offset.
 */
static void test_gnss_nav_pvt(void)
{
    AtlasGnss gnss;
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;
    AtlasGnssNavPvt result;
    uint8_t payload[92] = {0U};
    uint8_t frame[100];
    size_t length;

    memset(&gnss, 0, sizeof(gnss));
    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    gnss.transport = &transport;
    gnss.parser_state = ATLAS_GNSS_PARSE_SYNC1;
    transport.uart = &uart;
    transport.running = true;

    put_u32(&payload[0], 345678UL);
    put_u16(&payload[4], 2026U);
    payload[6] = 9U; payload[7] = 1U; payload[8] = 12U;
    payload[9] = 34U; payload[10] = 56U; payload[11] = 0x07U;
    payload[20] = 3U; payload[21] = 0x01U; payload[23] = 14U;
    put_u32(&payload[24], (uint32_t)(int32_t)-1221234567L);
    put_u32(&payload[28], (uint32_t)(int32_t)471234567L);
    put_u32(&payload[36], (uint32_t)(int32_t)123456L);
    put_u32(&payload[60], (uint32_t)(int32_t)9876L);
    put_u16(&payload[76], 123U);
    length = make_ubx(frame, 0x01U, 0x07U, payload, sizeof(payload));

    inject_uart(&transport, frame, length);
    CHECK(AtlasGnss_Service(&gnss, length) == ATLAS_OK);
    CHECK(AtlasGnss_GetLatestNavPvt(&gnss, &result, true));
    CHECK(result.year == 2026U);
    CHECK(result.fix_type == 3U);
    CHECK(result.satellites_used == 14U);
    CHECK(result.longitude_1e7_deg == -1221234567L);
    CHECK(result.latitude_1e7_deg == 471234567L);
    CHECK(result.height_msl_mm == 123456L);
    CHECK(result.ground_speed_mm_s == 9876L);
    CHECK(result.position_dop_0p01 == 123U);

    frame[length - 1U] ^= 0x01U;
    inject_uart(&transport, frame, length);
    CHECK(AtlasGnss_Service(&gnss, length) == ATLAS_OK);
    CHECK(gnss.health.checksum_errors == 1U);
    CHECK(!AtlasGnss_GetLatestNavPvt(&gnss, &result, true));
}

/** @brief Verify exact GNSS RAM readback acceptance and mismatch rejection. */
static void test_gnss_configuration_readback(void)
{
    AtlasGnss gnss;
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;

    memset(&gnss, 0, sizeof(gnss));
    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    gnss.transport = &transport;
    gnss.initialized = true;
    gnss.parser_state = ATLAS_GNSS_PARSE_SYNC1;
    transport.uart = &uart;
    transport.running = true;
    atlas_test_gnss_transport = &transport;
    atlas_test_gnss_readback_mismatch = false;
    AtlasTest_SetUartTransmitHook(gnss_configuration_reply);

    CHECK(AtlasGnss_ConfigureRam(&gnss, 100U, true) == ATLAS_OK);
    CHECK(gnss.health.acknowledgements == 1U);
    CHECK(gnss.health.configuration_readbacks == 1U);
    CHECK(gnss.health.configuration_mismatches == 0U);

    atlas_test_gnss_readback_mismatch = true;
    CHECK(AtlasGnss_ConfigureRam(&gnss, 100U, true) == ATLAS_ERROR_PROTOCOL);
    CHECK(gnss.health.configuration_readbacks == 1U);
    CHECK(gnss.health.configuration_mismatches == 1U);
    AtlasTest_SetUartTransmitHook(NULL);
    atlas_test_gnss_transport = NULL;
}

/** @brief Verify unsigned TIM2 subtraction across a 32-bit wrap. */
static void test_gnss_pps_wrap(void)
{
    AtlasGnss gnss;
    AtlasGnssPps snapshot;
    TIM_HandleTypeDef timer;

    memset(&gnss, 0, sizeof(gnss));
    memset(&timer, 0, sizeof(timer));
    gnss.pps_timer = &timer;
    CHECK(!AtlasGnss_GetPps(&gnss, &snapshot));
    timer.capture = 0xFFFFFF00UL;
    AtlasGnss_OnPpsCapture(&gnss);
    timer.capture = 0x000002E8UL;
    AtlasGnss_OnPpsCapture(&gnss);
    CHECK(gnss.pps.valid_period);
    CHECK(gnss.pps.period_us == 1000U);
    CHECK(gnss.pps.pulse_count == 2U);
    CHECK(AtlasGnss_GetPps(&gnss, &snapshot));
    CHECK(snapshot.latest_capture_us == 0x000002E8UL);
    CHECK(snapshot.period_us == 1000U);
    CHECK(snapshot.pulse_count == 2U);
    CHECK(snapshot.valid_period);
}

/** @brief Verify volatile BLE readback and persisted post-restart verification. */
static void test_ble_persistent_profile(void)
{
    AtlasBle ble;
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;

    memset(&ble, 0, sizeof(ble));
    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    transport.uart = &uart;
    transport.running = true;
    ble.transport = &transport;
    ble.initialized = true;
    ble.command_mode = true;
    atlas_test_ble_transport = &transport;
    atlas_test_ble_bad_readback = false;
    atlas_test_ble_profile_queries = 0U;
    atlas_test_ble_post_restart_queries = 0U;
    atlas_test_ble_stores = 0U;
    atlas_test_ble_poweroffs = 0U;
    atlas_test_ble_poweroff_seen = false;
    atlas_test_ble_transition_reply_enabled = true;
    AtlasTest_SetUartTransmitHook(ble_command_reply);
    AtlasTest_SetGpioWriteHook(ble_gpio_transition_reply);

    CHECK(AtlasBle_ConfigureSps(&ble, "Atlas-FGC", true) == ATLAS_OK);
    CHECK(!ble.command_mode);
    CHECK(ble.health.resets == 1U);
    CHECK(ble.health.mode_transitions == 2U);
    CHECK(ble.health.mode_transition_failures == 0U);
    CHECK(ble.health.configuration_mismatches == 0U);
    CHECK(atlas_test_ble_profile_queries == 8U);
    CHECK(atlas_test_ble_post_restart_queries == 4U);
    CHECK(atlas_test_ble_stores == 1U);
    CHECK(atlas_test_ble_poweroffs == 1U);

    /* Check both accepted readback forms and exact shortest/longest name lines.
     * The host reference uses libc formatting; production code deliberately does not. */
    const char *names[] = {"A", "ABCDEFGHIJKLMNOPQRSTUVWXYZ123"};
    ble.command_mode = true;
    for (size_t i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        char expected_command[64];
        atlas_test_ble_expected_name = names[i];
        atlas_test_ble_name_quoted = i != 0U;
        CHECK(AtlasBle_ConfigureSps(&ble, names[i], false) == ATLAS_OK);
        (void)snprintf(expected_command, sizeof(expected_command), "AT+UBTLN=\"%s\"", names[i]);
        CHECK(strcmp(atlas_test_ble_last_name_command, expected_command) == 0);
    }
    char unterminated_name[30];
    memset(unterminated_name, 'A', sizeof(unterminated_name));
    const uint32_t commands_before = ble.health.commands_sent;
    CHECK(AtlasBle_ConfigureSps(&ble, "", false) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBle_ConfigureSps(&ble, "bad\\name", false) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBle_ConfigureSps(&ble, "bad\"name", false) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBle_ConfigureSps(&ble, "bad,name", false) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBle_ConfigureSps(&ble, "bad\nname", false) == ATLAS_ERROR_ARGUMENT);
    CHECK(AtlasBle_ConfigureSps(&ble, unterminated_name, false) == ATLAS_ERROR_ARGUMENT);
    CHECK(ble.health.commands_sent == commands_before);
    atlas_test_ble_expected_name = "Atlas-FGC";
    atlas_test_ble_name_quoted = true;

    ble.command_mode = true;
    atlas_test_ble_bad_readback = true;
    atlas_test_ble_poweroff_seen = false;
    CHECK(AtlasBle_ConfigureSps(&ble, "Atlas-FGC", false) == ATLAS_ERROR_PROTOCOL);
    CHECK(ble.health.configuration_mismatches == 1U);
    CHECK(atlas_test_ble_stores == 1U);
    ble.command_mode = false;
    atlas_test_ble_transition_reply_enabled = false;
    CHECK(AtlasBle_EnterCommandMode(&ble) == ATLAS_ERROR_TIMEOUT);
    CHECK(!ble.command_mode);
    CHECK(ble.health.mode_transition_failures == 1U);
    AtlasTest_SetUartTransmitHook(NULL);
    AtlasTest_SetGpioWriteHook(NULL);
    atlas_test_ble_transport = NULL;
}

/** @brief Verify caller-buffer truncation is visible and cannot end in success. */
static void test_ble_response_overflow(void)
{
    AtlasBle ble;
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;
    char response[8];

    memset(&ble, 0, sizeof(ble));
    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    transport.uart = &uart;
    transport.running = true;
    ble.transport = &transport;
    ble.initialized = true;
    ble.command_mode = true;
    atlas_test_ble_transport = &transport;
    AtlasTest_SetUartTransmitHook(ble_command_reply);

    CHECK(AtlasBle_Command(&ble, "AT+LONG", response, sizeof(response), 100U) ==
          ATLAS_ERROR_OVERFLOW);
    CHECK(response[0] == '\0');
    CHECK(ble.health.response_overflows == 1U);
    CHECK(ble.health.command_ok == 0U);

    AtlasTest_SetUartTransmitHook(NULL);
    atlas_test_ble_transport = NULL;
}

/** @brief Verify SiK register readback, command echo tolerance, and mismatch rejection. */
static void test_rfd_parameter_readback(void)
{
    AtlasRfd900x radio;
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;
    char response[96];

    memset(&radio, 0, sizeof(radio));
    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    transport.uart = &uart;
    transport.running = true;
    radio.transport = &transport;
    radio.initialized = true;
    radio.command_mode = true;
    atlas_test_rfd_transport = &transport;
    atlas_test_rfd_bad_readback = false;
    atlas_test_rfd_empty_identity = false;
    atlas_test_rfd_stores = 0U;
    AtlasTest_SetUartTransmitHook(rfd_command_reply);

    CHECK(AtlasRfd900x_ReadIdentity(&radio, response, sizeof(response)) == ATLAS_OK);
    CHECK(strstr(response, "RFD900x") != NULL);
    CHECK(AtlasRfd900x_ReadSettings(&radio, response, sizeof(response)) == ATLAS_OK);
    CHECK(strstr(response, "S1: 57") != NULL);
    atlas_test_rfd_empty_identity = true;
    CHECK(AtlasRfd900x_ReadIdentity(&radio, response, sizeof(response)) ==
          ATLAS_ERROR_PROTOCOL);
    CHECK(radio.health.malformed_responses == 1U);

    CHECK(AtlasRfd900x_SetParameter(&radio, 5U, 42U, true) == ATLAS_OK);
    CHECK(atlas_test_rfd_stores == 1U);
    CHECK(radio.health.configuration_mismatches == 0U);
    atlas_test_rfd_bad_readback = true;
    CHECK(AtlasRfd900x_SetParameter(&radio, 5U, 42U, false) ==
          ATLAS_ERROR_PROTOCOL);
    CHECK(radio.health.configuration_mismatches == 1U);
    CHECK(atlas_test_rfd_stores == 1U);
    atlas_test_rfd_bad_readback = false;
    CHECK(AtlasRfd900x_SetParameter(&radio, 0U, 0U, false) == ATLAS_OK);
    CHECK(strcmp(atlas_test_rfd_last_write, "ATS0=0") == 0);
    CHECK(AtlasRfd900x_SetParameter(&radio, UINT8_MAX, UINT32_MAX, false) == ATLAS_OK);
    CHECK(strcmp(atlas_test_rfd_last_write, "ATS255=4294967295") == 0);
    AtlasTest_SetUartTransmitHook(NULL);
    atlas_test_rfd_transport = NULL;
}

/** @brief Verify staged-start stale RX recovery plus full-ring drop accounting. */
static void test_uart_overflow_accounting(void)
{
    AtlasUartTransport transport;
    UART_HandleTypeDef uart;

    memset(&transport, 0, sizeof(transport));
    memset(&uart, 0, sizeof(uart));
    CHECK(AtlasUartTransport_Init(&transport, &uart) == ATLAS_OK);
    AtlasTest_ResetUartReceiveTrace();
    AtlasTest_SetUartStaleReceive(true);
    CHECK(AtlasUartTransport_Start(&transport) == ATLAS_OK);
    CHECK(AtlasTest_GetUartAbortCount() == 1U);
    CHECK(AtlasTest_GetUartArmCount() == 1U);
    CHECK(transport.health.receive_preflights == 1U);
    CHECK(transport.health.start_retries == 0U);
    CHECK(transport.health.last_hal_status == HAL_OK);
    CHECK(transport.health.last_hal_error == 0U);
    CHECK(AtlasUartTransport_Stop(&transport) == ATLAS_OK);
    AtlasTest_SetUartArmFailures(1U);
    CHECK(AtlasUartTransport_Start(&transport) == ATLAS_OK);
    CHECK(AtlasTest_GetUartAbortCount() == 4U);
    CHECK(AtlasTest_GetUartArmCount() == 3U);
    CHECK(transport.health.receive_preflights == 3U);
    CHECK(transport.health.start_retries == 1U);
    CHECK(transport.health.last_hal_status == HAL_OK);
    CHECK(transport.health.last_hal_error == 0U);
    transport.rx_head = ATLAS_UART_RX_RING_CAPACITY - 1U;
    transport.rx_tail = 0U;
    transport.rx_chunk[0] = 0xA5U;
    HAL_UARTEx_RxEventCallback(&uart, 1U);
    CHECK(transport.health.dropped_bytes == 1U);
    CHECK(transport.rx_head == (ATLAS_UART_RX_RING_CAPACITY - 1U));
}

/** @brief Verify periodic due checks across ordinary and uint32_t-wrapped ticks. */
static void test_rtos_period_policy(void)
{
    CHECK(!AtlasRtosPolicy_PeriodDue(100U, 95U, 10U));
    CHECK(AtlasRtosPolicy_PeriodDue(105U, 95U, 10U));
    CHECK(!AtlasRtosPolicy_PeriodDue(105U, 95U, 0U));
    CHECK(AtlasRtosPolicy_PeriodDue(4U, UINT32_MAX - 5U, 10U));
    CHECK(!AtlasRtosPolicy_PeriodDue(4U, 0U, UINT32_C(0x80000000)));
    CHECK(!AtlasRtosPolicy_ResponseLate(109U, 100U, 10U));
    CHECK(AtlasRtosPolicy_ResponseLate(110U, 100U, 10U));
    CHECK(AtlasRtosPolicy_ResponseLate(150U, 100U, 10U)); /* Late release, quick hook. */
    CHECK(!AtlasRtosPolicy_ResponseLate(3U, UINT32_MAX - 5U, 10U));
    CHECK(AtlasRtosPolicy_ResponseLate(4U, UINT32_MAX - 5U, 10U));
    CHECK(AtlasRtosPolicy_ResponseLate(100U, 100U, 0U));
    CHECK(AtlasRtosPolicy_TimestampFresh(4U, UINT32_MAX - 5U, 10U));
    CHECK(!AtlasRtosPolicy_TimestampFresh(5U, UINT32_MAX - 5U, 10U));
    CHECK(!AtlasRtosPolicy_TimestampFresh(1U, 1U, UINT32_C(0x80000000)));
}

/** @brief Verify every independent watchdog policy gate fails closed. */
static void test_rtos_supervisor_policy(void)
{
    AtlasRtosSupervisorInput input;

    memset(&input, 0, sizeof(input));
    input.startup_status = ATLAS_OK;
    input.service_status = ATLAS_OK;
    input.sampling_status = ATLAS_OK;
    input.now_ms = 1000U;
    input.io_heartbeat = 10U;
    input.previous_io_heartbeat = 9U;
    input.application_heartbeat = 5U;
    input.previous_application_heartbeat = 4U;
    input.minimum_stack_free_words = 64U;
    input.io_stack_free_words = 300U;
    input.application_stack_free_words = 200U;
    input.supervisor_stack_free_words = 100U;
    input.idle_stack_free_words = 128U;
    input.sensors_fresh = true;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_NONE);

    input.startup_status = ATLAS_ERROR_IDENTITY;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_STARTUP);
    input.startup_status = ATLAS_OK;
    input.service_status = ATLAS_ERROR_IO;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_BOARD_SERVICE);
    input.service_status = ATLAS_OK;
    input.sampling_status = ATLAS_ERROR_TIMEOUT;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_SENSOR_SAMPLE);
    input.sampling_status = ATLAS_OK;

    input.sensors_fresh = false;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_SENSOR_STALE);
    input.sensors_fresh = true;

    input.io_busy = true;
    input.io_busy_until_ms = 1200U;
    input.sensors_fresh = false;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_NONE);
    input.io_busy_until_ms = 1000U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_IO_DEADLINE);
    input.now_ms = UINT32_MAX - 10U;
    input.io_busy_until_ms = 20U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_NONE);
    input.now_ms = 1000U;
    input.io_busy = false;
    input.sensors_fresh = true;

    input.previous_io_heartbeat = input.io_heartbeat;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_IO_STALLED);
    input.io_busy = true;
    input.io_busy_until_ms = 1200U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_NONE);
    input.sensors_fresh = false;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) == ATLAS_RTOS_FAULT_NONE);
    input.sensors_fresh = true;
    input.io_busy_until_ms = 1000U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_IO_DEADLINE);
    input.io_busy = false;
    input.previous_io_heartbeat = 9U;

    input.previous_application_heartbeat = input.application_heartbeat;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_APPLICATION_STALLED);
    input.previous_application_heartbeat = 4U;
    input.io_stack_free_words = 63U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_STACK_MARGIN);
    input.io_stack_free_words = 300U;
    input.idle_stack_free_words = 63U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_STACK_MARGIN);
    input.idle_stack_free_words = 128U;
    input.minimum_stack_free_words = 0U;
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(&input) ==
          ATLAS_RTOS_FAULT_STACK_MARGIN);
    CHECK(AtlasRtosPolicy_EvaluateSupervisor(NULL) == ATLAS_RTOS_FAULT_ASSERT);
}

/**
 * @brief Run every host test.
 * @return EXIT_SUCCESS when all checks pass.
 */
int main(void)
{
    test_adxl375_register_contract();
    test_lsm6dsv16b_register_contract();
    test_ms5611_reference_vector();
    test_mmc5983_register_contract();
    test_gnss_nav_pvt();
    test_gnss_configuration_readback();
    test_gnss_pps_wrap();
    test_ble_persistent_profile();
    test_ble_response_overflow();
    test_rfd_parameter_readback();
    test_bno085_i2c_contract();
    test_led_gpio_ownership();
    test_buzzer_differential_contract();
    test_uart_overflow_accounting();
    test_rtos_period_policy();
    test_rtos_supervisor_policy();
    if (atlas_test_failures != 0U)
    {
        fprintf(stderr, "%u test assertion(s) failed\n", atlas_test_failures);
        return EXIT_FAILURE;
    }
    puts("All Atlas host protocol tests passed.");
    return EXIT_SUCCESS;
}
