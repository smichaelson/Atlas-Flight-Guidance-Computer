/** @file test_console.c @brief Production console routing/serialization tests.
 * Major functions: main invokes the real dispatcher, result backpressure and
 * status formatter with inert boundary stubs. Emitted JSON is also consumed by
 * the actual laptop decoder. This is not an RTOS or electrical timing test. */
#include "service_model.h"
typedef struct
{
    int unused;
} IWDG_HandleTypeDef;
static struct
{
    uint32_t IDCODE;
} test_dbg;
#define DBGMCU (&test_dbg)
#define NVIC_PRIORITYGROUP_4 3U
static uint32_t SystemCoreClock = 200000000U;
uint32_t HAL_GetUIDw0(void)
{
    return 1U;
}
uint32_t HAL_GetUIDw1(void)
{
    return 2U;
}
uint32_t HAL_GetUIDw2(void)
{
    return 3U;
}
uint32_t HAL_NVIC_GetPriorityGrouping(void);
HAL_StatusTypeDef HAL_IWDG_Refresh(IWDG_HandleTypeDef *watchdog);
void vTaskStartScheduler(void);
#include "../../App/Inc/atlas_rtos.h"
/* Give the unused hardware scheduler entry internal linkage in this test TU.
 * Optimizing the unreachable hardware paths avoids fake success-driver stubs. */
static AtlasStatus AtlasBringup_Start(AtlasBoard *board, IWDG_HandleTypeDef *watchdog)
    __attribute__((unused));
#include "../../App/Src/atlas_bringup.c"
#include <stdio.h>

static AtlasIoSnapshot test_io;
static AtlasStorageHealth test_sd;
static AtlasUsbHealth test_usb;
static AtlasStorageResult test_storage_result;
static bool storage_result_ready;
static unsigned submitted_storage, submitted_gpio;
static AtlasIoCommand last_io_request;
static unsigned dfu_resets;
static unsigned dfu_stops;
static unsigned tone_requests;
static uint32_t tone_duration;
static AtlasStatus tone_result = ATLAS_OK;
AtlasStatus AtlasBuzzer_Beep(AtlasBuzzer *buzzer, uint32_t hz, uint32_t ms)
{
    assert(hz >= ATLAS_BUZZER_MIN_FREQUENCY_HZ && hz <= ATLAS_BUZZER_MAX_FREQUENCY_HZ);
    assert(ms > 0U && ms <= 1000U);
    ++tone_requests;
    tone_duration = ms;
    buzzer->running = true; /* Model a partially started timer even on failure. */
    buzzer->frequency_hz = hz;
    return tone_result;
}
void AtlasBuzzer_Stop(AtlasBuzzer *buzzer)
{
    buzzer->running = buzzer->timed = false;
}
void AtlasIo_EmergencyStop(void) { ++dfu_stops; }
void AtlasIo_BenchServoStop(void) { test_io.pwm_enabled_mask = 0U; }
void AtlasBoot_RequestDfu(void) { ++dfu_resets; }
bool AtlasIo_GetSnapshot(AtlasIoSnapshot *snapshot)
{
    *snapshot = test_io;
    return true;
}
bool AtlasStorage_GetHealth(AtlasStorageHealth *health)
{
    *health = test_sd;
    return true;
}
bool AtlasUsb_GetHealth(AtlasUsbHealth *health)
{
    *health = test_usb;
    return true;
}
AtlasStatus AtlasIo_Submit(const AtlasIoCommand *command, uint32_t *ticket)
{
    assert(command->type == ATLAS_IO_BENCH_GPIO || command->type == ATLAS_IO_BENCH_SERVO_ENABLE || command->type == ATLAS_IO_BENCH_SERVO_SET);
    last_io_request = *command;
    ++submitted_gpio;
    *ticket = 41U;
    return ATLAS_OK;
}
AtlasStatus AtlasStorage_Submit(const AtlasStorageRequest *request, uint32_t *ticket)
{
    assert(request->operation <= ATLAS_STORAGE_SELF_TEST);
    if (request->operation == ATLAS_STORAGE_READ)
    {
        assert(strcmp(request->filename, "ATLAS.TXT") == 0);
        assert(request->length == 24U); /* Read one byte beyond the 23-byte fixture. */
    }
    ++submitted_storage;
    *ticket = 42U;
    return ATLAS_OK;
}
bool AtlasIo_Receive(AtlasIoResult *result)
{
    (void)result;
    return false;
}
bool AtlasStorage_Receive(AtlasStorageResult *result)
{
    if (!storage_result_ready)
        return false;
    storage_result_ready = false;
    *result = test_storage_result;
    return true;
}

/** @brief Exercise the actual nonblocking melody scheduler with inert TIM15 calls. */
static void test_march(void)
{
    static AtlasBoard board;
    bench_board = &board;
    test_usb.configured = test_usb.dtr = true;
    watchdog_fault = 0U;
    uint32_t total = 0U;
    assert(BENCH_MARCH_NOTES == 42U);
    for (unsigned i = 0U; i < BENCH_MARCH_NOTES; ++i)
    {
        assert(bench_march[i].hz >= 300U && bench_march[i].hz <= 10000U);
        assert(bench_march[i].tone_ms > 0U && bench_march[i].tone_ms <= 1000U);
        assert(bench_march[i].gap_ms <= 540U);
        total += bench_march[i].tone_ms + bench_march[i].gap_ms;
    }
    assert(total == 16500U);
    assert(bench_march[0].hz == 392U && bench_march[3].hz == 311U && bench_march[4].hz == 466U);
    uint32_t phrase_end=0U;
    for(unsigned i=0U;i<18U;++i)
    {
        phrase_end+=bench_march[i].tone_ms+bench_march[i].gap_ms;
        if(i==8U)assert(phrase_end==4000U);
    }
    assert(phrase_end==8000U);
    /* Regression: two low Gs must return to high G before descending to F#. */
    const uint16_t bridge_hz[]={784U,392U,392U,784U,740U,698U,659U,622U,659U,415U,554U,523U,494U,466U,440U,466U};
    for(unsigned i=0U;i<sizeof(bridge_hz)/sizeof(bridge_hz[0]);++i)
        assert(bench_march[18U+i].hz==bridge_hz[i]);
    assert(bench_march[24].tone_ms+bench_march[24].gap_ms==125U);
    assert(bench_march[25].tone_ms+bench_march[25].gap_ms==125U);
    assert(bench_march[26].tone_ms==220U);
    /* Regression: the accepted closing phrase starts at 13.25 s, including F#. */
    const uint16_t ending_hz[] = {311U,370U,311U,466U,392U,311U,466U,392U};
    const uint16_t ending_ms[] = {250U,500U,375U,125U,500U,375U,125U,1000U};
    uint32_t ending_start = 0U;
    for (unsigned i = 0U; i < 34U; ++i)
        ending_start += bench_march[i].tone_ms + bench_march[i].gap_ms;
    assert(ending_start == 13250U);
    for (unsigned i = 0U; i < sizeof(ending_hz) / sizeof(ending_hz[0]); ++i)
    {
        assert(bench_march[34U+i].hz == ending_hz[i]);
        assert(bench_march[34U+i].tone_ms + bench_march[34U+i].gap_ms == ending_ms[i]);
    }
    /* Full timeline crosses HAL tick wrap, including every audible/rest boundary. */
    const uint32_t began = UINT32_MAX - 100U;
    test_tick = began;
    assert(bench_melody_start() == ATLAS_OK && melody_active);
    assert(bench_melody_start() == ATLAS_ERROR_BUSY && tone_requests == 1U);
    assert(!bench_dfu_ready());
    uint32_t offset = 0U;
    for (unsigned i = 0U; i < BENCH_MARCH_NOTES; ++i)
    {
        test_tick = began + offset;
        bench_melody_service();
        assert(melody_active && board.buzzer.running);
        assert(board.buzzer.frequency_hz == bench_march[i].hz);
        assert(tone_duration == bench_march[i].tone_ms && tone_requests == i + 1U);
        test_tick += bench_march[i].tone_ms;
        bench_melody_service();
        assert(!board.buzzer.running);
        offset += bench_march[i].tone_ms + bench_march[i].gap_ms;
    }
    test_tick = began + total;
    bench_melody_service();
    assert(!melody_active && tone_requests == 42U);
    test_tick += 50000U;
    bench_melody_service();
    assert(tone_requests == 42U); /* No looping/replay after completion. */

    /* Irregular service cannot accumulate tempo drift or drop a short note. */
    const unsigned before_jitter=tone_requests;
    assert(bench_melody_start()==ATLAS_OK);
    const uint32_t jitter_start=test_tick;
    const uint32_t steps[]={2U,3U,5U,7U,11U,17U,3U};
    for(unsigned step=0U;(uint32_t)(test_tick-jitter_start)<total;++step)
    {
        test_tick+=steps[step%(sizeof(steps)/sizeof(steps[0]))];
        bench_melody_service();
    }
    assert(!melody_active && tone_requests==before_jitter+42U);

    assert(bench_melody_start() == ATLAS_OK);
    const unsigned before = tone_requests;
    test_tick += 1100U; /* Skip the second note; shorten the third to its remaining time. */
    bench_melody_service();
    assert(tone_requests == before + 1U && melody_slot == 4U && tone_duration == 360U);
    test_tick += total;
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running && tone_requests == before + 1U);

    assert(bench_melody_start() == ATLAS_OK);
    bench_melody_stop();
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running);
    assert(bench_melody_start() == ATLAS_OK);
    test_usb.dtr = false;
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running);
    test_usb.dtr = true;
    assert(bench_melody_start() == ATLAS_OK);
    ++link_epoch;
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running);
    assert(bench_melody_start() == ATLAS_OK);
    watchdog_fault = 1U;
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running);
    watchdog_fault = 0U;
    /* Startup is one-shot without DTR, but faults and explicit stop still win. */
    test_usb.configured = test_usb.dtr = false;
    bench_startup_start();
    assert(melody_active && melody_startup && melody_track == 1U);
    ++link_epoch;
    test_tick += 120U;
    bench_melody_service();
    assert(melody_active && board.buzzer.frequency_hz == 1319U);
    test_tick += 1000U;
    bench_melody_service();
    assert(!melody_active && !board.buzzer.running);
    test_usb.configured = test_usb.dtr = true;
    tone_result = ATLAS_ERROR_IO;
    assert(bench_melody_start() == ATLAS_ERROR_IO);
    assert(!melody_active && !board.buzzer.running);
}

/** @brief Check dispatch, queue reservation and both ordinary/worst-case JSON.
 * @return Zero after assertions; stdout contains only valid target JSON lines. */
int main(void)
{
    TestRuntimeReset();
    test_scheduler = taskSCHEDULER_RUNNING;
    test_tick = 10000U;
    link_epoch = 1U;
    work_queue = xQueueCreateStatic(2U, sizeof(BenchWork), work_memory, &work_control);
    result_queue = xQueueCreateStatic(2U, sizeof(BenchReply), result_memory, &result_control);
    AtlasBenchCommand command;
    assert(AtlasBench_Parse("1 sd test", &command));
    bench_dispatch(&command);
    assert(submitted_storage == 1U && pending == BENCH_PENDING_SD);
    bench_dispatch(&command);
    assert(submitted_storage == 1U && reply_count == 1U); /* No ID replay. */
    assert(AtlasBench_Parse("2 gpio 1", &command));
    bench_dispatch(&command);
    assert(submitted_gpio == 0U && reply_count == 2U); /* One outstanding operation. */
    test_storage_result = (AtlasStorageResult){.ticket = 42U,
                                               .operation = ATLAS_STORAGE_SELF_TEST,
                                               .status = ATLAS_OK,
                                               .verified_bytes = 1024U};
    storage_result_ready = true;
    reply_count = BENCH_REPLY_COUNT;
    bench_results();
    assert(storage_result_ready && pending == BENCH_PENDING_SD);
    reply_count = 0U;
    bench_results();
    assert(!storage_result_ready && pending == BENCH_PENDING_NONE && reply_count == 1U);
    bench_send_reply();
    assert(strstr(tx, "\"verified_bytes\":1024") != NULL);
    for (unsigned variant = 0U; variant < 4U; ++variant)
    {
        command = (AtlasBenchCommand){.id = 3U + variant, .operation = ATLAS_BENCH_SD_READ};
        bench_dispatch(&command);
        assert(pending == BENCH_PENDING_SD);
        test_storage_result = (AtlasStorageResult){
            .ticket = 42U, .operation = ATLAS_STORAGE_READ, .status = ATLAS_OK, .length = 23U};
        memcpy(test_storage_result.data, "ATLAS SD READ TEST v1\r\n", 23U);
        if (variant == 1U)
            test_storage_result.length = 24U;
        if (variant == 2U)
            test_storage_result.length = 22U;
        if (variant == 3U)
            test_storage_result.data[0] = 'X';
        storage_result_ready = true;
        bench_results();
        assert(pending == BENCH_PENDING_NONE && reply_count == 1U);
        assert(reply_ring[reply_head].status == (variant == 0U ? ATLAS_OK : ATLAS_ERROR_PROTOCOL));
        assert(reply_ring[reply_head].verified_bytes == (variant == 0U ? 23U : 0U));
        bench_send_reply();
    }
    assert(AtlasBench_Parse("7 hello", &command));
    bench_dispatch(&command);
    assert(hello_due);
    ++link_epoch;
    BenchReply old = {.id = 9U, .epoch = 1U};
    unsigned before = reply_count;
    bench_reply(&old);
    assert(reply_count == before); /* Dropped cross-session completion. */
    reply_count = 0U;
    bench_hello();
    fputs(tx, stdout);
    test_usb = (AtlasUsbHealth){.configured = true, .dtr = true, .session = 1U};
    published.led_inhibited = 1U;
    published.sensors.adxl375_status = published.sensors.lsm6dsv16b_status =
        published.sensors.mmc5983ma_status = published.sensors.ms5611_status =
            ATLAS_ERROR_NOT_READY;
    bench_status();
    assert(strstr(tx, "serialization_overflow") == NULL && tx_length < BENCH_TX_CAPACITY);
    fputs(tx, stdout);
    /* Every bounded external identity byte can expand to six ASCII JSON bytes. */
    memset(published.ble_model, 1, sizeof(published.ble_model));
    memset(published.ble_firmware, 2, sizeof(published.ble_firmware));
    memset(published.gnss_version, 3, sizeof(published.gnss_version));
    published.ble_length = published.radio_length = 32U;
    memset(published.ble_rx, 0xAB, 32U);
    memset(published.radio_rx, 0xCD, 32U);
    for (unsigned i = 0; i < 4U; ++i)
    {
        published.count[i] = published.errors[i] = published.bno_count[i] = UINT32_MAX;
    }
    for (unsigned i = 0; i < 10U; ++i)
        test_io.analog.millivolts[i] = UINT32_MAX;
    test_tick = UINT32_MAX;
    frame_sequence = UINT32_MAX - 1U;
    published.attempted = 255U;
    bench_status();
    assert(strstr(tx, "serialization_overflow") == NULL && tx_length < BENCH_TX_CAPACITY);
    fputs(tx, stdout);
    pending = BENCH_PENDING_NONE;
    reply_count = 0U;
    last_id = 100U;
    test_usb.configured = test_usb.dtr = true;
    test_sd.mounted = false;
    test_io.pwm_enabled_mask = test_io.gpio_commanded_high = 0U;
    test_io.pyro.software_armed = false;
    worker_busy = false;
    watchdog_fault = 0U;
    assert(AtlasBench_Parse("101 bootloader 1 2 4", &command));
    bench_dispatch(&command);
    assert(!dfu_pending); /* Wrong target UID. */
    test_sd.mounted = true;
    assert(AtlasBench_Parse("102 bootloader 1 2 3", &command));
    bench_dispatch(&command);
    assert(!dfu_pending); /* Never reset a mounted filesystem. */
    test_sd.mounted = false;
    test_io.gpio_commanded_high = 1U;
    assert(AtlasBench_Parse("103 bootloader 1 2 3", &command));
    bench_dispatch(&command);
    assert(!dfu_pending);
    test_io.gpio_commanded_high = 0U;
    reply_count = 0U;
    assert(AtlasBench_Parse("104 bootloader 1 2 3", &command));
    bench_dispatch(&command);
    assert(dfu_pending && dfu_resets == 0U); /* Acceptance is not reset. */
    assert(AtlasBench_Parse("105 gpio 1", &command));
    const unsigned previous_gpio = submitted_gpio;
    bench_dispatch(&command);
    assert(submitted_gpio == previous_gpio);
    reply_count = 0U;
    tx_length = tx_offset = tx_queued = 100U;
    tx_completed_base = 200U;
    test_usb.tx_completed_bytes = 299U;
    dfu_drop_base = test_usb.tx_dropped_bytes;
    bench_dfu_service(&test_usb, true);
    assert(dfu_pending && dfu_resets == 0U); /* Last USB byte still pending. */
    test_usb.tx_completed_bytes = 300U;
    bench_dfu_service(&test_usb, true);
    assert(!dfu_pending && dfu_resets == 1U && dfu_stops == 1U);
    dfu_pending = true;
    bench_dfu_service(&test_usb, false);
    assert(!dfu_pending && dfu_resets == 1U); /* Disconnect cancels. */
    dfu_pending = true;
    ++test_usb.tx_dropped_bytes;
    bench_dfu_service(&test_usb, true);
    assert(!dfu_pending && dfu_resets == 1U); /* Lost ACK cancels. */
    dfu_pending = true;
    dfu_drop_base = test_usb.tx_dropped_bytes;
    dfu_started = test_tick - 3001U;
    bench_dfu_service(&test_usb, true);
    assert(!dfu_pending && dfu_resets == 1U); /* Finite deadline. */
    test_march();
    pending=BENCH_PENDING_NONE;reply_count=0U;watchdog_fault=0U;
    assert(AtlasBench_Parse("106 servo enable 8 1320 1720", &command));
    bench_dispatch(&command);
#if ATLAS_SERVO_BENCH
    assert(pending==BENCH_PENDING_GPIO && last_io_request.type==ATLAS_IO_BENCH_SERVO_ENABLE);
    assert(last_io_request.arguments.servo.channel==7U && last_io_request.arguments.servo.minimum_us==1320U && last_io_request.arguments.servo.maximum_us==1720U);
    pending=BENCH_PENDING_NONE;
    assert(AtlasBench_Parse("107 servo set 8 1600", &command));bench_dispatch(&command);
    assert(pending==BENCH_PENDING_GPIO && last_io_request.type==ATLAS_IO_BENCH_SERVO_SET && last_io_request.arguments.pwm.pulse_us==1600U);
    pending=BENCH_PENDING_SD;watchdog_fault=1U;test_io.pwm_enabled_mask=128U;
    assert(AtlasBench_Parse("108 servo stop", &command));bench_dispatch(&command);
    assert(test_io.pwm_enabled_mask==0U && pending==BENCH_PENDING_SD); /* OFF bypasses busy/fault without losing the old operation. */
#else
    assert(pending==BENCH_PENDING_NONE && reply_ring[reply_head].status==ATLAS_ERROR_UNSUPPORTED);
#endif
    return 0;
}
