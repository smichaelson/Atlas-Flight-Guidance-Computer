/**
 * @file atlas_bringup.c
 * @brief USB-connected, statically allocated diagnostic application for a new PCB.
 * Major functions:
 * - AtlasBringup_Start(): creates console/sensor/watchdog tasks and existing services.
 * - bench_owner_task(): alone probes and samples sensors, BLE, radio and expansion.
 * - bench_console_task(): frames allowlisted USB requests and publishes JSON lines.
 * - bench_watchdog_task(): supervises task progress, not expected component absence.
 * - bench_status(): serializes real measurements, ages, counters and explicit inhibits.
 * No automatic device probe, SD write, RF transmission, NVM save or actuation occurs.
 */
#include "atlas_bringup.h"
#include "atlas_build.h"
#include "atlas_bringup_protocol.h"
#include "atlas_expansion.h"
#include "atlas_rtos.h"
#include "atlas_storage.h"
#include "atlas_time.h"
#include "atlas_usb.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

#if ATLAS_BRINGUP
#define BENCH_STACK_WORDS 2048U
#define BENCH_OWNER_STACK_WORDS 2048U
#define BENCH_WATCH_STACK_WORDS 512U
#define BENCH_TX_CAPACITY 8192U
#define BENCH_REPLY_COUNT 4U
#define BENCH_PERIOD_MS 500U
#define BENCH_IO_ALIVE_MS 2000U
#define BENCH_OPERATION_MS 40000U
#define BENCH_TEST_TEXT "ATLAS_LINK_TEST_1\r\n"
#define BENCH_SD_TEXT "ATLAS SD READ TEST v1\r\n"

/** @brief One copied worker request, bound to the console connection generation. */
typedef struct
{
    AtlasBenchCommand command;
    uint32_t epoch;
} BenchWork;
/** @brief Retained completion; detail is escaped before sending to the laptop. */
typedef struct
{
    uint32_t id, epoch, verified_bytes;
    AtlasStatus status;
    char detail[160];
} BenchReply;
/** @brief Single-writer sensor publication; failed/stale values retain their timestamps. */
typedef struct
{
    AtlasBoardInitReport init;
    AtlasRtosSnapshot sensors;
    uint32_t attempted, published_ms, count[4], errors[4], bno_count[4];
    AtlasGnssHealth gnss_health;
    AtlasUartTransportHealth gnss_transport_health;
    AtlasBleHealth ble_health;
    AtlasBno085Health bno_health;
    uint32_t bno_pending_length, bno_intn_low, bno_initialized;
    uint32_t led_commanded, led_gates, led_initialized, led_inhibited;
    uint32_t ble_received, radio_received;
    uint8_t ble_rx[32], radio_rx[32], ble_length, radio_length;
    char ble_model[ATLAS_BLE_IDENTITY_CAPACITY], ble_firmware[ATLAS_BLE_IDENTITY_CAPACITY];
    char gnss_version[ATLAS_GNSS_VERSION_TEXT_CAPACITY];
    bool ble_command, ble_dtr, radio_command;
    uint32_t lsm_interrupts;
} BenchSamples;
/** @brief Distinct result owners; a disconnected request is drained, never replayed. */
typedef enum
{
    BENCH_PENDING_NONE = 0,
    BENCH_PENDING_WORK,
    BENCH_PENDING_SD,
    BENCH_PENDING_GPIO
} BenchPending;

static AtlasBoard *bench_board;
static IWDG_HandleTypeDef *bench_watchdog;
static QueueHandle_t work_queue, result_queue;
static StaticQueue_t work_control, result_control;
static uint8_t work_memory[2U * sizeof(BenchWork)], result_memory[2U * sizeof(BenchReply)];
static StaticTask_t console_control, owner_control, watchdog_control;
static StackType_t console_stack[BENCH_STACK_WORDS], owner_stack[BENCH_OWNER_STACK_WORDS];
static StackType_t watchdog_stack[BENCH_WATCH_STACK_WORDS];
static TaskHandle_t console_handle, owner_handle;
static BenchSamples working, published, console_sample;
static AtlasStatus io_start, storage_start, expansion_start;
static volatile uint32_t console_heartbeat, owner_heartbeat, link_epoch, worker_deadline;
static volatile uint32_t watchdog_refreshes, watchdog_fault;
static volatile bool worker_busy;
static AtlasBenchParser parser;
static char tx[BENCH_TX_CAPACITY];
static size_t tx_length, tx_offset;
static BenchReply reply_ring[BENCH_REPLY_COUNT];
static unsigned reply_head, reply_count;
static uint32_t frame_sequence, parser_errors, response_drops, last_id;
static BenchPending pending;
static uint32_t pending_ticket, pending_id, pending_epoch;
static bool hello_due;

/** @brief Copy a bounded literal/result string with guaranteed termination.
 * @param destination Output. @param capacity Bytes. @param source Input. */
static void bench_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0U)
        return;
    size_t i = 0U;
    while (i + 1U < capacity && source[i] != '\0')
    {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}
/** @brief Publish single-owner data atomically; no hardware access in console/watchdog. */
static void bench_publish(void)
{
    working.init = bench_board->init;
    working.attempted = bench_board->attempted_modules;
    working.published_ms = HAL_GetTick();
    working.gnss_health = bench_board->gnss.health;
    working.gnss_transport_health = bench_board->gnss_transport.health;
    working.ble_health = bench_board->ble.health;
    working.bno_health = bench_board->bno085.health;
    working.bno_pending_length = bench_board->bno085.pending_transfer_length;
    working.bno_intn_low =
        HAL_GPIO_ReadPin(BNO085_H_INTN_GPIO_Port, BNO085_H_INTN_Pin) == GPIO_PIN_RESET ? 1U : 0U;
    working.bno_initialized = bench_board->bno085.initialized ? 1U : 0U;
    working.led_commanded = (uint32_t)bench_board->led.color;
    working.led_gates = AtlasLed_ReadGateMask(&bench_board->led);
    working.led_initialized = bench_board->led.initialized ? 1U : 0U;
    working.led_inhibited = bench_board->led.output_inhibited ? 1U : 0U;
    working.lsm_interrupts = bench_board->lsm6dsv16b.health.interrupt_count;
    working.ble_command = bench_board->ble.command_mode;
    working.ble_dtr = AtlasBle_IsDtrAsserted(&bench_board->ble);
    working.radio_command = bench_board->radio.command_mode;
    memcpy(working.ble_model, bench_board->ble.model, sizeof(working.ble_model));
    memcpy(working.ble_firmware, bench_board->ble.firmware, sizeof(working.ble_firmware));
    memcpy(working.gnss_version, bench_board->gnss.software_version, sizeof(working.gnss_version));
    taskENTER_CRITICAL();
    published = working;
    taskEXIT_CRITICAL();
}
/** @brief Count successful NEW samples and actual transaction errors separately.
 * @param index Direct sensor index. @param status Transaction result. @param new_sample New data.
 */
static void bench_sample_result(unsigned index, AtlasStatus status, bool new_sample)
{
    if (new_sample && status == ATLAS_OK)
        ++working.count[index];
    if (status != ATLAS_OK && status != ATLAS_ERROR_NOT_READY)
        ++working.errors[index];
}
/** @brief Poll initialized devices; one failed device never suppresses another's diagnostics. */
static void bench_sample(void)
{
    static uint32_t last_direct, last_mmc, last_baro;
    AtlasRtosSnapshot *s = &working.sensors;
    const uint32_t now = HAL_GetTick();
    s->board_service_status = AtlasBoard_Service(bench_board);
    if ((uint32_t)(now - last_direct) >= 5U)
    {
        last_direct = now;
        bool ready = false;
        if (bench_board->init.adxl375 == ATLAS_OK)
        {
            s->adxl375_status = AtlasAdxl375_DataReady(&bench_board->adxl375, &ready);
            if (ready && s->adxl375_status == ATLAS_OK)
                s->adxl375_status = AtlasAdxl375_ReadSample(&bench_board->adxl375, &s->adxl375);
            bench_sample_result(0U, s->adxl375_status, ready);
        }
        ready = false;
        if (bench_board->init.lsm6dsv16b == ATLAS_OK)
        {
            s->lsm6dsv16b_status = AtlasLsm6dsv16b_DataReady(&bench_board->lsm6dsv16b, &ready);
            if (ready && s->lsm6dsv16b_status == ATLAS_OK)
                s->lsm6dsv16b_status =
                    AtlasLsm6dsv16b_ReadSample(&bench_board->lsm6dsv16b, &s->lsm6dsv16b);
            bench_sample_result(1U, s->lsm6dsv16b_status, ready);
        }
    }
    if ((uint32_t)(now - last_mmc) >= 100U && bench_board->init.mmc5983ma == ATLAS_OK)
    {
        last_mmc = now;
        s->mmc5983ma_status = AtlasMmc5983ma_ReadField(&bench_board->mmc5983ma, &s->mmc5983ma, 10U);
        bench_sample_result(2U, s->mmc5983ma_status, true);
    }
    else if ((uint32_t)(now - last_baro) >= 200U && bench_board->init.ms5611 == ATLAS_OK)
    {
        last_baro = now;
        s->ms5611_status =
            AtlasMs5611_Read(&bench_board->ms5611, ATLAS_MS5611_OSR_1024, &s->ms5611);
        bench_sample_result(3U, s->ms5611_status, true);
    }
    const sh2_SensorId_t ids[] = {SH2_ACCELEROMETER, SH2_GYROSCOPE_CALIBRATED,
                                  SH2_MAGNETIC_FIELD_CALIBRATED, SH2_ROTATION_VECTOR};
    sh2_SensorValue_t *const values[] = {&s->bno_accelerometer, &s->bno_gyroscope,
                                         &s->bno_magnetometer, &s->bno_rotation_vector};
    uint32_t *const stamps[] = {
        &s->bno_accelerometer_received_at_ms, &s->bno_gyroscope_received_at_ms,
        &s->bno_magnetometer_received_at_ms, &s->bno_rotation_vector_received_at_ms};
    for (unsigned i = 0U; i < 4U; ++i)
        if (AtlasBoard_GetBno085Sample(bench_board, ids[i], values[i], true))
        {
            *stamps[i] = HAL_GetTick();
            ++working.bno_count[i];
        }
    (void)AtlasGnss_GetLatestNavPvt(&bench_board->gnss, &s->gnss_nav_pvt, true);
    (void)AtlasGnss_GetPps(&bench_board->gnss, &s->gnss_pps);
    uint8_t bytes[32];
    const size_t ble = AtlasBle_ReadData(&bench_board->ble, bytes, sizeof(bytes));
    if (ble != 0U)
    {
        memcpy(working.ble_rx, bytes, ble);
        working.ble_length = (uint8_t)ble;
        working.ble_received += (uint32_t)ble;
    }
    const size_t radio = AtlasRfd900x_Read(&bench_board->radio, bytes, sizeof(bytes));
    if (radio != 0U)
    {
        memcpy(working.radio_rx, bytes, radio);
        working.radio_length = (uint8_t)radio;
        working.radio_received += (uint32_t)radio;
    }
}

/** @brief Exercise only an explicitly requested, externally wired expansion fixture.
 * @param command UART/SPI loopback or known I2C register. @param reply Detail output.
 * @return Real transfer/compare result, never an assertion that absent hardware passed. */
static AtlasStatus bench_expansion(const AtlasBenchCommand *command, BenchReply *reply)
{
    if (expansion_start != ATLAS_OK)
        return expansion_start;
    AtlasExpansionRequest request = {0};
    AtlasExpansionResult result;
    uint8_t received[32];
    uint32_t ticket;
    if (command->operation == ATLAS_BENCH_I2C_READ)
    {
        request.operation = ATLAS_EXP_I2C_REGISTER_READ;
        request.address_7bit = (uint8_t)command->argument[0];
        request.register_address = (uint16_t)command->argument[1];
        request.length = 1U;
    }
    else
    {
        request.operation = command->operation == ATLAS_BENCH_SPI_TEST ? ATLAS_EXP_SPI_EXCHANGE
                                                                       : ATLAS_EXP_UART_WRITE;
        request.length = sizeof(request.data);
        for (unsigned i = 0U; i < sizeof(request.data); ++i)
            request.data[i] = (uint8_t)(0xA5U ^ (i * 37U));
        /* A loopback must return this test's bytes, not a previous input backlog. */
        for (unsigned i = 0U; i < 32U && AtlasExpansion_ReadUart(received, sizeof(received)) != 0U;
             ++i)
        {
        }
    }
    AtlasStatus status = AtlasExpansion_Submit(&request, &ticket);
    if (status != ATLAS_OK)
        return status;
    (void)AtlasExpansion_Service(true); /* This function itself runs in the sole bus owner. */
    if (!AtlasExpansion_Receive(&result) || result.ticket != ticket)
        return ATLAS_ERROR_STATE;
    if (result.status != ATLAS_OK)
        return result.status;
    if (command->operation == ATLAS_BENCH_I2C_READ)
    {
        AtlasBenchJson text;
        AtlasBench_JsonInit(&text, reply->detail, sizeof(reply->detail));
        AtlasBench_JsonRaw(&text, "register_value_decimal=");
        AtlasBench_JsonU32(&text, result.data[0]);
        return result.length == 1U ? ATLAS_OK : ATLAS_ERROR_PROTOCOL;
    }
    if (command->operation == ATLAS_BENCH_SPI_TEST)
        return result.length == request.length &&
                       memcmp(result.data, request.data, request.length) == 0
                   ? ATLAS_OK
                   : ATLAS_ERROR_PROTOCOL;
    size_t used = 0U;
    const uint32_t began = HAL_GetTick();
    while (used < sizeof(received) && (uint32_t)(HAL_GetTick() - began) < 100U)
    {
        (void)AtlasExpansion_Service(false);
        used += AtlasExpansion_ReadUart(received + used, sizeof(received) - used);
        if (used < sizeof(received))
            AtlasTime_DelayMs(1U);
    }
    if (used != request.length)
        return ATLAS_ERROR_TIMEOUT;
    return memcmp(received, request.data, used) == 0 ? ATLAS_OK : ATLAS_ERROR_PROTOCOL;
}

/** @brief Execute one allowed device action; no arbitrary driver command is exposed.
 * @param command Copied request. @param reply Optional diagnostic detail.
 * @return Driver result. */
static AtlasStatus bench_execute(const AtlasBenchCommand *command, BenchReply *reply)
{
    switch (command->operation)
    {
    case ATLAS_BENCH_PROBE:
        return AtlasBoard_ProbeModule(bench_board, (AtlasBoardModule)command->argument[0]);
    case ATLAS_BENCH_LED:
        return AtlasLed_SetColor(&bench_board->led, (AtlasLedColor)command->argument[0]);
    case ATLAS_BENCH_BEEP:
        return AtlasBuzzer_Beep(&bench_board->buzzer, 4800U, 200U);
    case ATLAS_BENCH_STOP:
        AtlasBuzzer_Stop(&bench_board->buzzer);
        return AtlasLed_SetColor(&bench_board->led, ATLAS_LED_OFF);
    case ATLAS_BENCH_BLE_PROFILE:
        return AtlasBle_ConfigureSps(&bench_board->ble, "AtlasBench", false);
    case ATLAS_BENCH_BLE_DATA:
        return AtlasBle_EnterDataMode(&bench_board->ble);
    case ATLAS_BENCH_BLE_COMMAND:
        return AtlasBle_EnterCommandMode(&bench_board->ble);
    case ATLAS_BENCH_BLE_PING:
        return AtlasBle_WriteData(&bench_board->ble, (const uint8_t *)BENCH_TEST_TEXT,
                                  sizeof(BENCH_TEST_TEXT) - 1U, 10U);
    case ATLAS_BENCH_RADIO_ID:
    {
        AtlasStatus status = AtlasRfd900x_EnterCommandMode(&bench_board->radio);
        if (status == ATLAS_OK)
            status = AtlasRfd900x_ReadIdentity(&bench_board->radio, reply->detail,
                                               sizeof(reply->detail));
        if (bench_board->radio.command_mode)
        {
            const AtlasStatus exit_status = AtlasRfd900x_ExitCommandMode(&bench_board->radio);
            if (status == ATLAS_OK)
                status = exit_status;
        }
        return status;
    }
    case ATLAS_BENCH_RADIO_PING:
        return AtlasRfd900x_Write(&bench_board->radio, (const uint8_t *)BENCH_TEST_TEXT,
                                  sizeof(BENCH_TEST_TEXT) - 1U, 10U);
    case ATLAS_BENCH_UART_TEST:
    case ATLAS_BENCH_SPI_TEST:
    case ATLAS_BENCH_I2C_READ:
        return bench_expansion(command, reply);
    default:
        return ATLAS_ERROR_UNSUPPORTED;
    }
}

/** @brief Own all sensor/link work; publish before returning command completion.
 * @param argument Unused. */
static void bench_owner_task(void *argument)
{
    (void)argument;
    for (;;)
    {
        bench_sample();
        (void)AtlasExpansion_Service(false);
        BenchWork work;
        if (uxQueueSpacesAvailable(result_queue) != 0U &&
            xQueueReceive(work_queue, &work, 0U) == pdTRUE)
        {
            BenchReply reply = {.id = work.command.id, .epoch = work.epoch};
            AtlasUsbHealth usb;
            if (watchdog_fault != 0U || work.epoch != link_epoch || !AtlasUsb_GetHealth(&usb) ||
                !usb.configured || !usb.dtr)
                reply.status = ATLAS_ERROR_STATE;
            else
            {
                /* A previously scheduled beep must not last through a slow AT probe. */
                AtlasBuzzer_Stop(&bench_board->buzzer);
                taskENTER_CRITICAL();
                worker_deadline = HAL_GetTick() + BENCH_OPERATION_MS;
                worker_busy = true;
                taskEXIT_CRITICAL();
                reply.status = bench_execute(&work.command, &reply);
                taskENTER_CRITICAL();
                /* Returning between supervisor ticks must not hide an overrun. */
                if ((uint32_t)(worker_deadline - HAL_GetTick()) >= UINT32_C(0x80000000) ||
                    worker_deadline == HAL_GetTick())
                {
                    if (watchdog_fault == 0U)
                        watchdog_fault = 6U;
                    reply.status = ATLAS_ERROR_TIMEOUT;
                }
                ++owner_heartbeat;
                worker_busy = false;
                taskEXIT_CRITICAL();
            }
            bench_publish();
            configASSERT(xQueueSend(result_queue, &reply, 0U) == pdTRUE);
        }
        bench_publish();
        ++owner_heartbeat;
        vTaskDelay(pdMS_TO_TICKS(2U));
    }
}

/** @brief Add one numeric JSON field to an already-open object.
 * @param json Writer. @param name Trusted field name. @param value Integer. */
static void bench_field(AtlasBenchJson *json, const char *name, uint32_t value)
{
    AtlasBench_JsonRaw(json, ",\"");
    AtlasBench_JsonRaw(json, name);
    AtlasBench_JsonRaw(json, "\":");
    AtlasBench_JsonU32(json, value);
}
/** @brief Write a three-component fixed-point array.
 * @param json Writer. @param x X. @param y Y. @param z Z. @param scale Unit multiplier. */
static void bench_vector(AtlasBenchJson *json, float x, float y, float z, uint32_t scale)
{
    AtlasBench_JsonRaw(json, "[");
    AtlasBench_JsonScaled(json, x, scale);
    AtlasBench_JsonRaw(json, ",");
    AtlasBench_JsonScaled(json, y, scale);
    AtlasBench_JsonRaw(json, ",");
    AtlasBench_JsonScaled(json, z, scale);
    AtlasBench_JsonRaw(json, "]");
}
/** @brief Emit a bounded hex string for bytes that may contain any value.
 * @param json Writer. @param bytes Input. @param count Bytes. */
static void bench_hex(AtlasBenchJson *json, const uint8_t *bytes, size_t count)
{
    static const char digits[] = "0123456789ABCDEF";
    AtlasBench_JsonRaw(json, "\"");
    for (size_t i = 0U; i < count; ++i)
    {
        char pair[3] = {digits[bytes[i] >> 4], digits[bytes[i] & 15U], '\0'};
        AtlasBench_JsonRaw(json, pair);
    }
    AtlasBench_JsonRaw(json, "\"");
}
/** @brief Finish a whole JSON record, or replace it with an explicit serialization error.
 * @param json Completed writer. */
static void bench_finish(AtlasBenchJson *json)
{
    AtlasBench_JsonRaw(json, "}\n");
    if (!json->ok)
    {
        ++response_drops;
        bench_text(tx, sizeof(tx), "{\"type\":\"error\",\"reason\":\"serialization_overflow\"}\n");
        tx_length = strlen(tx);
    }
    else
        tx_length = json->used;
    tx_offset = 0U;
}
/** @brief Build the version/profile handshake; UID is device identity, not authentication. */
static void bench_hello(void)
{
    AtlasBenchJson json;
    AtlasBench_JsonInit(&json, tx, sizeof(tx));
    /* Leading LF terminates any partial record left in the host on reconnect. */
    AtlasBench_JsonRaw(
        &json, "\n{\"type\":\"hello\",\"profile\":\"bringup\",\"version\":\"" ATLAS_BRINGUP_VERSION
               "\",\"pwm_pyro_inhibited\":true,\"led_inhibited\":true");
    bench_field(&json, "schema", ATLAS_BENCH_SCHEMA);
    bench_field(&json, "clock_hz", SystemCoreClock);
    bench_field(&json, "device_id", DBGMCU->IDCODE);
    AtlasBench_JsonRaw(&json, ",\"uid\":[");
    AtlasBench_JsonU32(&json, HAL_GetUIDw0());
    AtlasBench_JsonRaw(&json, ",");
    AtlasBench_JsonU32(&json, HAL_GetUIDw1());
    AtlasBench_JsonRaw(&json, ",");
    AtlasBench_JsonU32(&json, HAL_GetUIDw2());
    AtlasBench_JsonRaw(&json, "]");
    bench_finish(&json);
}
/** @brief Serialize real sensor/rail/transport state; zero counts do not imply valid values. */
static void bench_status(void)
{
    AtlasBenchJson j;
    AtlasIoSnapshot io = {0};
    AtlasStorageHealth sd = {0};
    AtlasUsbHealth usb = {0};
    taskENTER_CRITICAL();
    console_sample = published;
    taskEXIT_CRITICAL();
    const BenchSamples *b = &console_sample;
    const AtlasRtosSnapshot *s = &b->sensors;
    const bool io_available = io_start == ATLAS_OK && AtlasIo_GetSnapshot(&io);
    (void)AtlasStorage_GetHealth(&sd);
    (void)AtlasUsb_GetHealth(&usb);
    AtlasBench_JsonInit(&j, tx, sizeof(tx));
    AtlasBench_JsonRaw(&j, "{\"type\":\"status\",\"profile\":\"bringup\",\"inhibited\":true");
    bench_field(&j, "schema", ATLAS_BENCH_SCHEMA);
    bench_field(&j, "seq", ++frame_sequence);
    bench_field(&j, "ms", HAL_GetTick());
    bench_field(&j, "owner_ms", b->published_ms);
    bench_field(&j, "attempted", b->attempted);
    bench_field(&j, "pending_id", pending == BENCH_PENDING_NONE ? 0U : pending_id);
    const AtlasStatus init[] = {b->init.adxl375,   b->init.lsm6dsv16b,
                                b->init.mmc5983ma, b->init.ms5611,
                                b->init.bno085,    b->init.bno085_default_reports,
                                b->init.gnss,      b->init.gnss_ram_configuration,
                                b->init.ble,       b->init.radio_transport,
                                b->init.led,       b->init.buzzer};
    AtlasBench_JsonRaw(&j, ",\"init\":[");
    for (unsigned i = 0U; i < sizeof(init) / sizeof(init[0]); ++i)
    {
        if (i != 0U)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, (uint32_t)init[i]);
    }
    AtlasBench_JsonRaw(&j, "],\"count\":[");
    for (unsigned i = 0U; i < 4U; ++i)
    {
        if (i)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, b->count[i]);
    }
    AtlasBench_JsonRaw(&j, "],\"errors\":[");
    for (unsigned i = 0U; i < 4U; ++i)
    {
        if (i)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, b->errors[i]);
    }
    AtlasBench_JsonRaw(&j, "],\"sample_status\":[");
    AtlasBench_JsonU32(&j, s->adxl375_status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->lsm6dsv16b_status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->mmc5983ma_status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->ms5611_status);
    AtlasBench_JsonRaw(&j, "],\"service\":");
    AtlasBench_JsonU32(&j, s->board_service_status);
    AtlasBench_JsonRaw(&j, ",\"adxl\":{\"mg\":");
    bench_vector(&j, s->adxl375.x_g, s->adxl375.y_g, s->adxl375.z_g, 1000U);
    bench_field(&j, "t", s->adxl375.timestamp_ms);
    AtlasBench_JsonRaw(&j, "},\"lsm\":{\"mg\":");
    bench_vector(&j, s->lsm6dsv16b.accel_x_g, s->lsm6dsv16b.accel_y_g, s->lsm6dsv16b.accel_z_g,
                 1000U);
    AtlasBench_JsonRaw(&j, ",\"mdps\":");
    bench_vector(&j, s->lsm6dsv16b.gyro_x_dps, s->lsm6dsv16b.gyro_y_dps, s->lsm6dsv16b.gyro_z_dps,
                 1000U);
    AtlasBench_JsonRaw(&j, ",\"temp_cc\":");
    AtlasBench_JsonScaled(&j, s->lsm6dsv16b.temperature_c, 100U);
    bench_field(&j, "t", s->lsm6dsv16b.timestamp_ms);
    bench_field(&j, "irq", b->lsm_interrupts);
    AtlasBench_JsonRaw(&j, "},\"mmc\":{\"nt\":");
    bench_vector(&j, s->mmc5983ma.x_gauss, s->mmc5983ma.y_gauss, s->mmc5983ma.z_gauss, 100000U);
    bench_field(&j, "t", s->mmc5983ma.timestamp_ms);
    AtlasBench_JsonRaw(&j, "},\"baro\":{\"pa\":");
    AtlasBench_JsonI32(&j, s->ms5611.pressure_pa);
    AtlasBench_JsonRaw(&j, ",\"temp_cc\":");
    AtlasBench_JsonI32(&j, s->ms5611.temperature_centi_c);
    bench_field(&j, "t", s->ms5611.timestamp_ms);
    AtlasBench_JsonRaw(&j, "},\"bno\":{\"count\":[");
    for (unsigned i = 0U; i < 4U; ++i)
    {
        if (i)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, b->bno_count[i]);
    }
    AtlasBench_JsonRaw(&j, "],\"t\":[");
    AtlasBench_JsonU32(&j, s->bno_accelerometer_received_at_ms);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_gyroscope_received_at_ms);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_magnetometer_received_at_ms);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_rotation_vector_received_at_ms);
    AtlasBench_JsonRaw(&j, "],\"accuracy\":[");
    AtlasBench_JsonU32(&j, s->bno_accelerometer.status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_gyroscope.status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_magnetometer.status);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, s->bno_rotation_vector.status);
    AtlasBench_JsonRaw(&j, "],\"accel_mm_s2\":");
    bench_vector(&j, s->bno_accelerometer.un.accelerometer.x,
                 s->bno_accelerometer.un.accelerometer.y, s->bno_accelerometer.un.accelerometer.z,
                 1000U);
    AtlasBench_JsonRaw(&j, ",\"gyro_mrad_s\":");
    bench_vector(&j, s->bno_gyroscope.un.gyroscope.x, s->bno_gyroscope.un.gyroscope.y,
                 s->bno_gyroscope.un.gyroscope.z, 1000U);
    AtlasBench_JsonRaw(&j, ",\"mag_nt\":");
    bench_vector(&j, s->bno_magnetometer.un.magneticField.x, s->bno_magnetometer.un.magneticField.y,
                 s->bno_magnetometer.un.magneticField.z, 1000U);
    AtlasBench_JsonRaw(&j, ",\"q_ppm\":[");
    AtlasBench_JsonScaled(&j, s->bno_rotation_vector.un.rotationVector.real, 1000000U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, s->bno_rotation_vector.un.rotationVector.i, 1000000U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, s->bno_rotation_vector.un.rotationVector.j, 1000000U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, s->bno_rotation_vector.un.rotationVector.k, 1000000U);
    AtlasBench_JsonRaw(&j, "],\"health\":{\"interrupts\":");
    AtlasBench_JsonU32(&j, b->bno_health.interrupts);
    bench_field(&j, "reads", b->bno_health.transfers_read);
    bench_field(&j, "writes", b->bno_health.transfers_written);
    bench_field(&j, "io_errors", b->bno_health.io_errors);
    bench_field(&j, "protocol_errors", b->bno_health.protocol_errors);
    bench_field(&j, "decoded", b->bno_health.decoded_samples);
    bench_field(&j, "decode_errors", b->bno_health.decode_errors);
    bench_field(&j, "resets", b->bno_health.async_resets);
    bench_field(&j, "recovery_attempts", b->bno_health.bus_recovery_attempts);
    bench_field(&j, "recovery_failures", b->bno_health.bus_recovery_failures);
    bench_field(&j, "last_hal_status", b->bno_health.last_hal_status);
    bench_field(&j, "last_hal_error", b->bno_health.last_hal_error);
    bench_field(&j, "failure_stage", b->bno_health.last_failure_stage);
    bench_field(&j, "last_length", b->bno_health.last_transfer_length);
    bench_field(&j, "pending_length", b->bno_pending_length);
    bench_field(&j, "intn_low", b->bno_intn_low);
    bench_field(&j, "initialized", b->bno_initialized);
    const AtlasGnssNavPvt *g = &s->gnss_nav_pvt;
    AtlasBench_JsonRaw(&j, "}},\"gnss\":{\"version\":");
    AtlasBench_JsonString(&j, b->gnss_version, sizeof(b->gnss_version));
    bench_field(&j, "t", g->received_at_ms);
    bench_field(&j, "frames", b->gnss_health.nav_pvt_frames);
    bench_field(&j, "crc_errors", b->gnss_health.checksum_errors);
    bench_field(&j, "timeouts", b->gnss_health.frame_timeouts);
    bench_field(&j, "fix", g->fix_type);
    bench_field(&j, "flags", g->flags);
    bench_field(&j, "sv", g->satellites_used);
    AtlasBench_JsonRaw(&j, ",\"lat_e7\":");
    AtlasBench_JsonI32(&j, g->latitude_1e7_deg);
    AtlasBench_JsonRaw(&j, ",\"lon_e7\":");
    AtlasBench_JsonI32(&j, g->longitude_1e7_deg);
    AtlasBench_JsonRaw(&j, ",\"h_msl_mm\":");
    AtlasBench_JsonI32(&j, g->height_msl_mm);
    bench_field(&j, "hacc_mm", g->horizontal_accuracy_mm);
    bench_field(&j, "tow_ms", g->time_of_week_ms);
    bench_field(&j, "pps_count", s->gnss_pps.pulse_count);
    bench_field(&j, "pps_us", s->gnss_pps.period_us);
    bench_field(&j, "failure_stage", b->gnss_health.last_failure_stage);
    bench_field(&j, "failure_status", b->gnss_health.last_failure_status);
    bench_field(&j, "pps_started", b->gnss_health.pps_capture_started ? 1U : 0U);
    bench_field(&j, "rx_bytes", b->gnss_transport_health.bytes_received);
    bench_field(&j, "tx_bytes", b->gnss_transport_health.bytes_transmitted);
    bench_field(&j, "dropped", b->gnss_transport_health.dropped_bytes);
    bench_field(&j, "uart_errors", b->gnss_transport_health.uart_errors);
    bench_field(&j, "restarts", b->gnss_transport_health.receive_restarts);
    bench_field(&j, "preflights", b->gnss_transport_health.receive_preflights);
    bench_field(&j, "start_retries", b->gnss_transport_health.start_retries);
    bench_field(&j, "hal_status", b->gnss_transport_health.last_hal_status);
    bench_field(&j, "hal_error", b->gnss_transport_health.last_hal_error);
    AtlasBench_JsonRaw(&j, "},\"power\":{\"available\":");
    AtlasBench_JsonRaw(&j, io_available ? "true" : "false");
    bench_field(&j, "start", io_start);
    bench_field(&j, "status", io.status);
    bench_field(&j, "t", io.analog.sampled_at_ms);
    bench_field(&j, "count", io.analog.sequence);
    bench_field(&j, "valid", io.analog.valid_mask);
    bench_field(&j, "vdda_mv", io.analog.vdda_mv);
    AtlasBench_JsonRaw(&j, ",\"temp_c\":");
    AtlasBench_JsonI32(&j, io.analog.die_temperature_c);
    AtlasBench_JsonRaw(&j, ",\"mv\":[");
    for (unsigned i = 0U; i < ATLAS_ANALOG_CHANNELS; ++i)
    {
        if (i)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, io.analog.millivolts[i]);
    }
    AtlasBench_JsonRaw(&j, "],\"raw\":[");
    for (unsigned i = 0U; i < ATLAS_ANALOG_CHANNELS; ++i)
    {
        if (i)
            AtlasBench_JsonRaw(&j, ",");
        AtlasBench_JsonU32(&j, io.analog.raw[i]);
    }
    AtlasBench_JsonRaw(&j, "]");
    bench_field(&j, "adc_errors", io.adc_errors);
    bench_field(&j, "ref_stage", (uint32_t)io.reference_failure_stage);
    bench_field(&j, "ref_channel", io.reference_temperature_channel ? 1U : 0U);
    bench_field(&j, "ref_raw", io.reference_raw);
    bench_field(&j, "ref_hal_status", io.reference_hal_status);
    bench_field(&j, "ref_hal_error", io.reference_hal_error);
    bench_field(&j, "reset_flags", io.reset_flags);
    bench_field(&j, "power_events", io.power_events);
    bench_field(&j, "ecc_events", io.ecc_events);
    AtlasBench_JsonRaw(&j, "},\"gpio\":{\"inputs\":");
    AtlasBench_JsonU32(&j, io.gpio_inputs);
    bench_field(&j, "outputs", io.gpio_commanded_high);
    bench_field(&j, "switch", io.external_switch ? 1U : 0U);
    bench_field(&j, "pwm", io.pwm_enabled_mask);
    bench_field(&j, "armed", io.pyro.software_armed ? 1U : 0U);
    AtlasBench_JsonRaw(&j, "},\"led\":{\"commanded\":");
    AtlasBench_JsonU32(&j, b->led_commanded);
    bench_field(&j, "gates", b->led_gates);
    bench_field(&j, "initialized", b->led_initialized);
    bench_field(&j, "inhibited", b->led_inhibited);
    AtlasBench_JsonRaw(&j, "},\"sd\":{\"start\":");
    AtlasBench_JsonU32(&j, storage_start);
    bench_field(&j, "card", sd.card_detected ? 1U : 0U);
    bench_field(&j, "mounted", sd.mounted ? 1U : 0U);
    bench_field(&j, "status", sd.last_status);
    bench_field(&j, "fs", sd.filesystem_result);
    bench_field(&j, "completed", sd.completed_requests);
    bench_field(&j, "errors", sd.errors);
    bench_field(&j, "time_valid", sd.time_valid ? 1U : 0U);
    AtlasBench_JsonRaw(&j, ",\"utc\":[");
    AtlasBench_JsonU32(&j, sd.utc.year);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.utc.month);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.utc.day);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.utc.hour);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.utc.minute);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.utc.second);
    AtlasBench_JsonRaw(&j, "]},\"ble\":{\"model\":");
    AtlasBench_JsonString(&j, b->ble_model, sizeof(b->ble_model));
    AtlasBench_JsonRaw(&j, ",\"firmware\":");
    AtlasBench_JsonString(&j, b->ble_firmware, sizeof(b->ble_firmware));
    bench_field(&j, "command", b->ble_command ? 1U : 0U);
    bench_field(&j, "dtr", b->ble_dtr ? 1U : 0U);
    bench_field(&j, "rx", b->ble_received);
    bench_field(&j, "tx", b->ble_health.data_bytes_written);
    bench_field(&j, "timeouts", b->ble_health.command_timeouts);
    AtlasBench_JsonRaw(&j, ",\"last_hex\":");
    bench_hex(&j, b->ble_rx, b->ble_length);
    AtlasBench_JsonRaw(&j, "},\"radio\":{\"rx\":");
    AtlasBench_JsonU32(&j, b->radio_received);
    bench_field(&j, "command", b->radio_command ? 1U : 0U);
    AtlasBench_JsonRaw(&j, ",\"last_hex\":");
    bench_hex(&j, b->radio_rx, b->radio_length);
    AtlasBench_JsonRaw(&j, "},\"usb\":{\"session\":");
    AtlasBench_JsonU32(&j, usb.session);
    bench_field(&j, "rx", usb.rx_bytes);
    bench_field(&j, "rx_drop", usb.rx_dropped_bytes);
    bench_field(&j, "tx", usb.tx_completed_bytes);
    bench_field(&j, "tx_drop", usb.tx_dropped_bytes);
    bench_field(&j, "timeouts", usb.tx_timeouts);
    AtlasBench_JsonRaw(&j, "},\"tasks\":{\"console\":");
    AtlasBench_JsonU32(&j, console_heartbeat);
    bench_field(&j, "owner", owner_heartbeat);
    bench_field(&j, "watchdog", watchdog_refreshes);
    bench_field(&j, "fault", watchdog_fault);
    bench_field(&j, "busy", worker_busy ? 1U : 0U);
    bench_field(&j, "parser_errors", parser_errors);
    bench_field(&j, "response_drops", response_drops);
    AtlasBench_JsonRaw(&j, ",\"stack_words\":[");
    AtlasBench_JsonU32(&j, uxTaskGetStackHighWaterMark(console_handle));
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, uxTaskGetStackHighWaterMark(owner_handle));
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, io.stack_free_words);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, sd.stack_free_words);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonU32(&j, usb.stack_free_words);
    AtlasBench_JsonRaw(&j, "]}");
    bench_finish(&j);
}

/** @brief Enqueue a reply only for the active connection; never replay old completions.
 * @param reply Completed result. */
static void bench_reply(const BenchReply *reply)
{
    if (reply->epoch != link_epoch)
        return;
    if (reply_count == BENCH_REPLY_COUNT)
    {
        ++response_drops;
        return;
    }
    reply_ring[(reply_head + reply_count) % BENCH_REPLY_COUNT] = *reply;
    ++reply_count;
}
/** @brief Compose the oldest retained command result. */
static void bench_send_reply(void)
{
    const BenchReply *reply = &reply_ring[reply_head];
    AtlasBenchJson j;
    AtlasBench_JsonInit(&j, tx, sizeof(tx));
    AtlasBench_JsonRaw(&j, "{\"type\":\"reply\",\"id\":");
    AtlasBench_JsonU32(&j, reply->id);
    bench_field(&j, "status", reply->status);
    bench_field(&j, "verified_bytes", reply->verified_bytes);
    AtlasBench_JsonRaw(&j, ",\"name\":");
    AtlasBench_JsonString(&j, AtlasStatus_Name(reply->status), 48U);
    AtlasBench_JsonRaw(&j, ",\"detail\":");
    AtlasBench_JsonString(&j, reply->detail, sizeof(reply->detail));
    bench_finish(&j);
    reply_head = (reply_head + 1U) % BENCH_REPLY_COUNT;
    --reply_count;
}
/** @brief Route one request; busy/old IDs never execute and are never automatically retried.
 * @param command Parsed command. */
static void bench_dispatch(const AtlasBenchCommand *command)
{
    BenchReply reply = {.id = command->id, .epoch = link_epoch, .status = ATLAS_OK};
    if (command->id <= last_id)
    {
        reply.status = ATLAS_ERROR_STATE;
        bench_text(reply.detail, sizeof(reply.detail), "old_id_no_replay");
        bench_reply(&reply);
        return;
    }
    last_id = command->id;
    if (command->operation == ATLAS_BENCH_HELLO || command->operation == ATLAS_BENCH_STATUS)
    {
        if (command->operation == ATLAS_BENCH_HELLO)
            hello_due = true;
        bench_text(reply.detail, sizeof(reply.detail), "bringup_schema_1");
        bench_reply(&reply);
        return;
    }
    if (pending != BENCH_PENDING_NONE || watchdog_fault != 0U)
    {
        reply.status = ATLAS_ERROR_BUSY;
        bench_reply(&reply);
        return;
    }
    pending_id = command->id;
    pending_epoch = link_epoch;
    if (command->operation == ATLAS_BENCH_GPIO)
    {
        AtlasIoCommand request = {.type = ATLAS_IO_BENCH_GPIO};
        request.arguments.gpio.channel =
            command->argument[0] == 0U ? 0U : (uint8_t)(command->argument[0] - 1U);
        request.arguments.gpio.high = command->argument[0] != 0U;
        reply.status = AtlasIo_Submit(&request, &pending_ticket);
        if (reply.status == ATLAS_OK)
            pending = BENCH_PENDING_GPIO;
    }
    else if ((command->operation >= ATLAS_BENCH_SD_MOUNT && command->operation <= ATLAS_BENCH_UTC))
    {
        AtlasStorageRequest request = {0};
        switch (command->operation)
        {
        case ATLAS_BENCH_SD_MOUNT:
            request.operation = ATLAS_STORAGE_MOUNT;
            break;
        case ATLAS_BENCH_SD_UNMOUNT:
            request.operation = ATLAS_STORAGE_UNMOUNT;
            break;
        case ATLAS_BENCH_SD_TEST:
            request.operation = ATLAS_STORAGE_SELF_TEST;
            break;
        case ATLAS_BENCH_SD_READ:
            request.operation = ATLAS_STORAGE_READ;
            memcpy(request.filename, "ATLAS.TXT", 10U);
            request.length = sizeof(BENCH_SD_TEXT);
            break; /* Includes one extra byte to reject a longer fixture. */
        case ATLAS_BENCH_UTC:
            request.operation = ATLAS_STORAGE_SET_UTC;
            request.utc = (AtlasUtc){(uint16_t)command->argument[0], (uint8_t)command->argument[1],
                                     (uint8_t)command->argument[2],  (uint8_t)command->argument[3],
                                     (uint8_t)command->argument[4],  (uint8_t)command->argument[5]};
            break;
        default:
            reply.status = ATLAS_ERROR_ARGUMENT;
            break;
        }
        if (reply.status == ATLAS_OK)
            reply.status = AtlasStorage_Submit(&request, &pending_ticket);
        if (reply.status == ATLAS_OK)
            pending = BENCH_PENDING_SD;
    }
    else
    {
        const BenchWork work = {*command, link_epoch};
        if (xQueueSend(work_queue, &work, 0U) == pdTRUE)
            pending = BENCH_PENDING_WORK;
        else
            reply.status = ATLAS_ERROR_BUSY;
    }
    if (pending == BENCH_PENDING_NONE)
        bench_reply(&reply);
}
/** @brief Drain all service results even when the USB client has disconnected. */
static void bench_results(void)
{
    /* Leave results with their owner until a reply slot can be reserved. */
    if (reply_count == BENCH_REPLY_COUNT)
        return;
    BenchReply reply;
    if (xQueueReceive(result_queue, &reply, 0U) == pdTRUE)
    {
        if (pending == BENCH_PENDING_WORK && pending_id == reply.id && pending_epoch == reply.epoch)
            pending = BENCH_PENDING_NONE;
        bench_reply(&reply);
    }
    AtlasStorageResult storage;
    if (AtlasStorage_Receive(&storage) && pending == BENCH_PENDING_SD &&
        storage.ticket == pending_ticket)
    {
        reply = (BenchReply){.id = pending_id,
                             .epoch = pending_epoch,
                             .status = storage.status,
                             .verified_bytes = storage.verified_bytes};
        if (storage.operation == ATLAS_STORAGE_READ && storage.status == ATLAS_OK)
        {
            if (storage.length != sizeof(BENCH_SD_TEXT) - 1U ||
                memcmp(storage.data, BENCH_SD_TEXT, sizeof(BENCH_SD_TEXT) - 1U) != 0)
                reply.status = ATLAS_ERROR_PROTOCOL;
            else
                reply.verified_bytes = storage.length;
        }
        AtlasBenchJson detail;
        AtlasBench_JsonInit(&detail, reply.detail, sizeof(reply.detail));
        AtlasBench_JsonRaw(&detail, "FatFs=");
        AtlasBench_JsonU32(&detail, storage.filesystem_result);
        if (storage.filesystem_result == 8U)
            AtlasBench_JsonRaw(
                &detail,
                " existing ATLASCHK.TST preserved; remove on laptop only if no longer needed");
        pending = BENCH_PENDING_NONE;
        bench_reply(&reply);
    }
    AtlasIoResult io;
    if (AtlasIo_Receive(&io) && pending == BENCH_PENDING_GPIO && io.ticket == pending_ticket)
    {
        reply = (BenchReply){.id = pending_id, .epoch = pending_epoch, .status = io.status};
        bench_text(reply.detail, sizeof(reply.detail),
                   "logic GPIO request completed; any HIGH auto-clears after 1000 ms; verify "
                   "pin/input electrically");
        pending = BENCH_PENDING_NONE;
        bench_reply(&reply);
    }
}
/** @brief Handle framing, session changes and loss without accessing sensor drivers.
 * @param argument Unused. */
static void bench_console_task(void *argument)
{
    (void)argument;
    bool connected = false;
    uint32_t session = 0U, dropped = 0U, last_status = HAL_GetTick();
    for (;;)
    {
        AtlasUsbHealth usb = {0};
        (void)AtlasUsb_GetHealth(&usb);
        const bool online = usb.configured && usb.dtr;
        if (online != connected || usb.session != session)
        {
            ++link_epoch;
            connected = online;
            session = usb.session;
            last_id = 0U;
            AtlasBench_Reset(&parser, false);
            tx_length = tx_offset = 0U;
            reply_head = reply_count = 0U;
            hello_due = online;
            dropped = usb.rx_dropped_bytes;
        }
        if (usb.rx_dropped_bytes != dropped)
        {
            AtlasBench_Reset(&parser, true);
            dropped = usb.rx_dropped_bytes;
            ++parser_errors;
        }
        bench_results();
        uint8_t input[64];
        const size_t received = AtlasUsb_Read(input, sizeof(input));
        for (size_t i = 0U; i < received; ++i)
        {
            AtlasBenchCommand command;
            const int framed = AtlasBench_Feed(&parser, input[i], &command);
            if (framed < 0)
                ++parser_errors;
            if (framed == 1 && online)
            {
                /* Reserve a possible immediate reply before any side effect. */
                if (reply_count < BENCH_REPLY_COUNT)
                    bench_dispatch(&command);
                else
                    ++response_drops;
            }
        }
        if (online && tx_offset == tx_length)
        {
            tx_length = tx_offset = 0U;
            if (hello_due)
            {
                bench_hello();
                hello_due = false;
            }
            else if (reply_count != 0U)
                bench_send_reply();
            else if ((uint32_t)(HAL_GetTick() - last_status) >= BENCH_PERIOD_MS)
            {
                last_status = HAL_GetTick();
                bench_status();
            }
        }
        if (online && tx_offset < tx_length)
        {
            size_t length = tx_length - tx_offset;
            if (length > ATLAS_USB_PACKET_CAPACITY)
                length = ATLAS_USB_PACKET_CAPACITY;
            if (AtlasUsb_Write((const uint8_t *)tx + tx_offset, length) == ATLAS_OK)
                tx_offset += length;
        }
        ++console_heartbeat;
        vTaskDelay(pdMS_TO_TICKS(2U));
    }
}
/** @brief Keep diagnostics alive on component failures, but not on a hung task/operation.
 * @param argument Unused. */
static void bench_watchdog_task(void *argument)
{
    (void)argument;
    uint32_t console_seen = 0U, owner_seen = 0U, io_seen = 0U;
    uint32_t console_at = HAL_GetTick(), owner_at = console_at, io_at = console_at;
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100U));
        AtlasIoSnapshot io = {0};
        const bool io_ok = io_start == ATLAS_OK && AtlasIo_GetSnapshot(&io);
        const uint32_t now = HAL_GetTick();
        if (console_seen != console_heartbeat)
        {
            console_seen = console_heartbeat;
            console_at = now;
        }
        if (owner_seen != owner_heartbeat)
        {
            owner_seen = owner_heartbeat;
            owner_at = now;
        }
        if (io_ok && io_seen != io.heartbeat)
        {
            io_seen = io.heartbeat;
            io_at = now;
        }
        const bool busy_in_time = worker_busy &&
                                  (uint32_t)(worker_deadline - now) < UINT32_C(0x80000000) &&
                                  worker_deadline != now;
        if (watchdog_fault == 0U && (uint32_t)(now - console_at) > BENCH_IO_ALIVE_MS)
            watchdog_fault = 1U;
        if (watchdog_fault == 0U && !busy_in_time && (uint32_t)(now - owner_at) > BENCH_IO_ALIVE_MS)
            watchdog_fault = 2U;
        if (watchdog_fault == 0U && io_start == ATLAS_OK &&
            (uint32_t)(now - io_at) > BENCH_IO_ALIVE_MS)
            watchdog_fault = 3U;
        if (watchdog_fault == 0U &&
            (uxTaskGetStackHighWaterMark(console_handle) < 64U ||
             uxTaskGetStackHighWaterMark(owner_handle) < 64U ||
             uxTaskGetStackHighWaterMark(NULL) < 64U || (io_ok && io.stack_free_words < 64U)))
            watchdog_fault = 4U;
        if (watchdog_fault == 0U)
        {
            if (HAL_IWDG_Refresh(bench_watchdog) == HAL_OK)
                ++watchdog_refreshes;
            else
                watchdog_fault = 5U;
        }
        else
            AtlasRtos_InhibitOutputs();
    }
}
#endif

/** @brief Start only the isolated bench scheduler, never the flight hook.
 * @param board Prepared board. @param watchdog Running independent watchdog.
 * @return Startup error; a running scheduler never returns. */
AtlasStatus AtlasBringup_Start(AtlasBoard *board, IWDG_HandleTypeDef *watchdog)
{
#if ATLAS_BRINGUP
    if (board == NULL || watchdog == NULL)
        return ATLAS_ERROR_NULL;
    if (!board->init_complete || board->attempted_modules != 0U ||
        xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ||
        HAL_NVIC_GetPriorityGrouping() != NVIC_PRIORITYGROUP_4)
        return ATLAS_ERROR_STATE;
    bench_board = board;
    bench_watchdog = watchdog;
    working.sensors.adxl375_status = working.sensors.lsm6dsv16b_status =
        working.sensors.mmc5983ma_status = working.sensors.ms5611_status = ATLAS_ERROR_NOT_READY;
    working.init = board->init;
    published = working;
    work_queue = xQueueCreateStatic(2U, sizeof(BenchWork), work_memory, &work_control);
    result_queue = xQueueCreateStatic(2U, sizeof(BenchReply), result_memory, &result_control);
    if (work_queue == NULL || result_queue == NULL)
        return ATLAS_ERROR_STATE;
    if (AtlasUsb_Start() != ATLAS_OK)
        return ATLAS_ERROR_IO;
    /* A failed analog calibration remains visible via USB instead of preventing
     * the scheduler from starting. Its emergency inhibit is never cleared. */
    io_start = AtlasIo_Start(&board->hardware.io);
    storage_start = AtlasStorage_Start(board->hardware.rtc);
    expansion_start = AtlasExpansion_Start(board->hardware.expansion_uart,
                                           board->hardware.expansion_i2c, board->hardware.imu_spi);
    console_handle = xTaskCreateStatic(bench_console_task, "AtlasBenchUSB", BENCH_STACK_WORDS, NULL,
                                       3U, console_stack, &console_control);
    owner_handle = xTaskCreateStatic(bench_owner_task, "AtlasBenchIO", BENCH_OWNER_STACK_WORDS,
                                     NULL, 1U, owner_stack, &owner_control);
    TaskHandle_t watch =
        xTaskCreateStatic(bench_watchdog_task, "AtlasBenchWatch", BENCH_WATCH_STACK_WORDS, NULL, 5U,
                          watchdog_stack, &watchdog_control);
    if (console_handle == NULL || owner_handle == NULL || watch == NULL)
        return ATLAS_ERROR_STATE;
    vTaskStartScheduler();
    return ATLAS_ERROR_STATE;
#else
    (void)board;
    (void)watchdog;
    return ATLAS_ERROR_UNSUPPORTED;
#endif
}
