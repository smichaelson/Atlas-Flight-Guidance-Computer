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
    assert(command->type == ATLAS_IO_BENCH_GPIO);
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
    return 0;
}
