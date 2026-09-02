/**
 * @file atlas_board.c
 * @brief Complete Atlas module bring-up, default volatile configuration, and ISR dispatch.
 *
 * Major functions:
 * - AtlasBoard_Init(): attempts all modules and preserves a per-device bring-up report.
 * - AtlasBoard_Service(): keeps UART/BNO protocols and buzzer scheduling responsive.
 * - AtlasBoard_LatchRuntimeFault(): latches task-level runtime failures for supervision.
 * - AtlasBoard_GetBno085Sample(): provides typed, latest-value access to fused reports.
 * - HAL_GPIO_EXTI_Callback()/HAL_TIM_IC_CaptureCallback(): minimal board ISR routing.
 */

#include "atlas_board.h"

#include <string.h>

static AtlasBoard *atlas_active_board;

/**
 * @brief Preserve the first failure while allowing later initialization to continue.
 * @param overall Current aggregate status.
 * @param candidate New subsystem result.
 * @return candidate when overall was OK and candidate failed; otherwise overall.
 */
static AtlasStatus atlas_board_accumulate(AtlasStatus overall,
                                          AtlasStatus candidate)
{
    return ((overall == ATLAS_OK) && (candidate != ATLAS_OK)) ? candidate : overall;
}

/**
 * @brief Store the latest decoded BNO085 report by its sensor ID.
 * @param context Atlas board instance.
 * @param sample Decoded SH-2 sensor sample.
 */
static void atlas_board_bno085_sample(void *context,
                                      const sh2_SensorValue_t *sample)
{
    AtlasBoard *board = (AtlasBoard *)context;

    if ((board == NULL) || (sample == NULL) ||
        (sample->sensorId > SH2_MAX_SENSOR_ID))
    {
        return;
    }
    board->bno085_latest[sample->sensorId] = *sample;
    board->bno085_available_mask |= (UINT64_C(1) << sample->sensorId);
}

/**
 * @brief Enable the reviewed BNO085 baseline report set.
 * @param board Board containing an initialized BNO085.
 * @return ATLAS_OK or the first SH-2 report configuration failure.
 */
static AtlasStatus atlas_board_enable_bno_reports(AtlasBoard *board)
{
    AtlasStatus overall = ATLAS_OK;
    AtlasStatus status;

    status = AtlasBno085_EnableReport(&board->bno085,
                                      SH2_ACCELEROMETER,
                                      10000U,
                                      0U);
    overall = atlas_board_accumulate(overall, status);
    status = AtlasBno085_EnableReport(&board->bno085,
                                      SH2_GYROSCOPE_CALIBRATED,
                                      10000U,
                                      0U);
    overall = atlas_board_accumulate(overall, status);
    status = AtlasBno085_EnableReport(&board->bno085,
                                      SH2_MAGNETIC_FIELD_CALIBRATED,
                                      20000U,
                                      0U);
    overall = atlas_board_accumulate(overall, status);
    status = AtlasBno085_EnableReport(&board->bno085,
                                      SH2_ROTATION_VECTOR,
                                      10000U,
                                      0U);
    return atlas_board_accumulate(overall, status);
}

/**
 * @brief Initialize every supported sensor/module and retain every individual result.
 * @param board Destination board instance.
 * @param hardware Initialized HAL handles matching the schematic.
 * @return ATLAS_OK when every required check passes, otherwise the first failure.
 */
AtlasStatus AtlasBoard_Init(AtlasBoard *board, const AtlasBoardHardware *hardware)
{
    AtlasLsm6dsv16bConfig imu_config;
    AtlasStatus overall = ATLAS_OK;

    if ((board == NULL) || (hardware == NULL) ||
        (hardware->sensor_i2c == NULL) ||
        (hardware->shared_sensor_spi == NULL) ||
        (hardware->imu_spi == NULL) ||
        (hardware->gnss_uart == NULL) ||
        (hardware->radio_uart == NULL) ||
        (hardware->ble_uart == NULL) ||
        (hardware->microsecond_pps_timer == NULL) ||
        (hardware->buzzer_timer == NULL))
    {
        return ATLAS_ERROR_NULL;
    }

    memset(board, 0, sizeof(*board));
    atlas_active_board = board;

    board->init.led = AtlasLed_Init(&board->led);
    overall = atlas_board_accumulate(overall, board->init.led);
    if (board->init.led == ATLAS_OK)
    {
        (void)AtlasLed_SetColor(&board->led, ATLAS_LED_BLUE);
    }

    board->init.buzzer = AtlasBuzzer_Init(&board->buzzer,
                                           hardware->buzzer_timer);
    overall = atlas_board_accumulate(overall, board->init.buzzer);

    board->init.adxl375 = AtlasAdxl375_Init(&board->adxl375,
                                            hardware->shared_sensor_spi,
                                            CS_ADXL375_GPIO_Port,
                                            CS_ADXL375_Pin,
                                            ATLAS_ADXL375_ODR_400_HZ);
    overall = atlas_board_accumulate(overall, board->init.adxl375);

    imu_config = AtlasLsm6dsv16b_DefaultConfig();
    board->init.lsm6dsv16b = AtlasLsm6dsv16b_Init(&board->lsm6dsv16b,
                                                   hardware->imu_spi,
                                                   CS_LSM6DSV16B_GPIO_Port,
                                                   CS_LSM6DSV16B_Pin,
                                                   &imu_config);
    overall = atlas_board_accumulate(overall, board->init.lsm6dsv16b);

    board->init.mmc5983ma = AtlasMmc5983ma_Init(&board->mmc5983ma,
                                                hardware->shared_sensor_spi,
                                                CS_MMC5983_GPIO_Port,
                                                CS_MMC5983_Pin,
                                                ATLAS_MMC5983_BW_200_HZ_4_MS);
    overall = atlas_board_accumulate(overall, board->init.mmc5983ma);

    board->init.ms5611 = AtlasMs5611_Init(&board->ms5611,
                                          hardware->sensor_i2c);
    overall = atlas_board_accumulate(overall, board->init.ms5611);

    board->init.bno085 = AtlasBno085_Init(&board->bno085,
                                          hardware->sensor_i2c,
                                          hardware->microsecond_pps_timer,
                                          atlas_board_bno085_sample,
                                          board);
    overall = atlas_board_accumulate(overall, board->init.bno085);
    board->init.bno085_default_reports = (board->init.bno085 == ATLAS_OK) ?
                                          atlas_board_enable_bno_reports(board) :
                                          ATLAS_ERROR_NOT_READY;
    overall = atlas_board_accumulate(overall,
                                     board->init.bno085_default_reports);

    board->init.gnss = AtlasGnss_Init(&board->gnss,
                                      &board->gnss_transport,
                                      hardware->gnss_uart,
                                      hardware->microsecond_pps_timer);
    overall = atlas_board_accumulate(overall, board->init.gnss);
    board->init.gnss_ram_configuration = (board->init.gnss == ATLAS_OK) ?
        AtlasGnss_ConfigureRam(&board->gnss, 100U, true) :
        ATLAS_ERROR_NOT_READY;
    overall = atlas_board_accumulate(overall,
                                     board->init.gnss_ram_configuration);

    /* Radio init is intentionally transport-only: no +++, AT command, or NVM write. */
    board->init.radio_transport = AtlasRfd900x_Init(&board->radio,
                                                    &board->radio_transport,
                                                    hardware->radio_uart);
    overall = atlas_board_accumulate(overall, board->init.radio_transport);

    /* BLE identity probing is read-only; persistent SPS setup remains an explicit API. */
    board->init.ble = AtlasBle_Init(&board->ble,
                                    &board->ble_transport,
                                    hardware->ble_uart);
    overall = atlas_board_accumulate(overall, board->init.ble);

    board->init_complete = true;
    if (board->led.initialized)
    {
        (void)AtlasLed_SetColor(&board->led,
                                (overall == ATLAS_OK) ?
                                ATLAS_LED_GREEN : ATLAS_LED_YELLOW);
    }
    return overall;
}

/**
 * @brief Service all initialized interrupt-driven transports and scheduled outputs once.
 * @param board Initialized board instance.
 * @return ATLAS_OK when service paths remain healthy, otherwise the first latched failure.
 */
AtlasStatus AtlasBoard_Service(AtlasBoard *board)
{
    AtlasStatus overall = ATLAS_OK;
    AtlasStatus status;

    if (board == NULL)
    {
        return ATLAS_ERROR_NULL;
    }
    if (!board->init_complete)
    {
        return ATLAS_ERROR_NOT_READY;
    }
    if (board->runtime_fault != ATLAS_OK)
    {
        AtlasBuzzer_Service(&board->buzzer);
        return board->runtime_fault;
    }

    if (board->bno085.initialized)
    {
        status = AtlasBno085_Service(&board->bno085);
        overall = atlas_board_accumulate(overall, status);
    }
    if (board->init.gnss == ATLAS_OK)
    {
        status = AtlasGnss_Service(&board->gnss, 512U);
        overall = atlas_board_accumulate(overall, status);
    }
    if (board->init.radio_transport == ATLAS_OK)
    {
        status = AtlasUartTransport_Service(board->radio.transport);
        overall = atlas_board_accumulate(overall, status);
    }
    if (board->init.ble == ATLAS_OK)
    {
        status = AtlasUartTransport_Service(board->ble.transport);
        overall = atlas_board_accumulate(overall, status);
    }
    AtlasBuzzer_Service(&board->buzzer);
    ++board->service_cycles;
    board->runtime_fault = atlas_board_accumulate(board->runtime_fault, overall);
    return board->runtime_fault;
}

/**
 * @brief Preserve the first non-OK runtime failure.
 * @param board Initialized board instance.
 * @param status Runtime status to latch; ATLAS_OK is ignored.
 */
void AtlasBoard_LatchRuntimeFault(AtlasBoard *board, AtlasStatus status)
{
    if ((board != NULL) && (status != ATLAS_OK) &&
        (board->runtime_fault == ATLAS_OK))
    {
        board->runtime_fault = status;
    }
}

/**
 * @brief Report whether every attempted startup step passed.
 * @param board Board instance with completed initialization.
 * @return true only when all report entries are ATLAS_OK.
 */
bool AtlasBoard_AllStartupStepsPassed(const AtlasBoard *board)
{
    if ((board == NULL) || !board->init_complete)
    {
        return false;
    }
    return (board->init.led == ATLAS_OK) &&
           (board->init.buzzer == ATLAS_OK) &&
           (board->init.adxl375 == ATLAS_OK) &&
           (board->init.lsm6dsv16b == ATLAS_OK) &&
           (board->init.mmc5983ma == ATLAS_OK) &&
           (board->init.ms5611 == ATLAS_OK) &&
           (board->init.bno085 == ATLAS_OK) &&
           (board->init.bno085_default_reports == ATLAS_OK) &&
           (board->init.gnss == ATLAS_OK) &&
           (board->init.gnss_ram_configuration == ATLAS_OK) &&
           (board->init.radio_transport == ATLAS_OK) &&
           (board->init.ble == ATLAS_OK);
}

/**
 * @brief Copy the latest decoded BNO085 report for one SH-2 sensor ID.
 * @param board Initialized board instance.
 * @param sensor_id SH-2 sensor/report identifier.
 * @param sample Destination value.
 * @param consume true to clear the availability bit.
 * @return true when at least one report of this type has been received.
 * @note Foreground-only accessor paired with AtlasBoard_Service().
 */
bool AtlasBoard_GetBno085Sample(AtlasBoard *board,
                                sh2_SensorId_t sensor_id,
                                sh2_SensorValue_t *sample,
                                bool consume)
{
    const uint64_t mask = (sensor_id <= SH2_MAX_SENSOR_ID) ?
                          (UINT64_C(1) << sensor_id) : UINT64_C(0);

    if ((board == NULL) || (sample == NULL) || (mask == 0U) ||
        ((board->bno085_available_mask & mask) == 0U))
    {
        return false;
    }
    *sample = board->bno085_latest[sensor_id];
    if (consume)
    {
        board->bno085_available_mask &= ~mask;
    }
    return true;
}

/**
 * @brief Route GPIO interrupt sources to their allocation-free ISR handlers.
 * @param gpio_pin STM32 HAL pin mask.
 */
void HAL_GPIO_EXTI_Callback(uint16_t gpio_pin)
{
    if (atlas_active_board == NULL)
    {
        return;
    }
    if (gpio_pin == BNO085_H_INTN_Pin)
    {
        AtlasBno085_OnInterrupt(&atlas_active_board->bno085);
    }
    else if (gpio_pin == LSM6_INT1_Pin)
    {
        AtlasLsm6dsv16b_OnInterrupt(&atlas_active_board->lsm6dsv16b);
    }
    else
    {
        /* Other generated EXTI sources remain available to future application code. */
    }
}

/**
 * @brief Route TIM2 channel-1 capture events to the GNSS PPS tracker.
 * @param timer HAL timer that generated the callback.
 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer)
{
    if ((atlas_active_board != NULL) &&
        (timer == atlas_active_board->gnss.pps_timer) &&
        (timer->Channel == HAL_TIM_ACTIVE_CHANNEL_1))
    {
        AtlasGnss_OnPpsCapture(&atlas_active_board->gnss);
    }
}
