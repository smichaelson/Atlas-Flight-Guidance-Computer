/**
 * @file atlas_bringup_protocol.h
 * @brief Heap-free bench command framing and checked JSON-lines serialization.
 * Major functions: AtlasBench_Feed/Parse reject malformed commands; the Json*
 * functions construct bounded, ASCII JSON without libc formatted I/O.
 * No arbitrary memory/register write, reset, bootloader, PWM or pyro command exists.
 */
#ifndef ATLAS_BRINGUP_PROTOCOL_H
#define ATLAS_BRINGUP_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ATLAS_BENCH_LINE_CAPACITY 96U
#define ATLAS_BENCH_SCHEMA 1U
/** @brief Fixed allowlist; argument meanings are documented in docs/startup.md. */
typedef enum
{
    ATLAS_BENCH_HELLO = 0,
    ATLAS_BENCH_STATUS,
    ATLAS_BENCH_PROBE,
    ATLAS_BENCH_LED,
    ATLAS_BENCH_BEEP,
    ATLAS_BENCH_STOP,
    ATLAS_BENCH_GPIO,
    ATLAS_BENCH_SD_MOUNT,
    ATLAS_BENCH_SD_READ,
    ATLAS_BENCH_SD_TEST,
    ATLAS_BENCH_SD_UNMOUNT,
    ATLAS_BENCH_UTC,
    ATLAS_BENCH_BLE_PROFILE,
    ATLAS_BENCH_BLE_DATA,
    ATLAS_BENCH_BLE_COMMAND,
    ATLAS_BENCH_BLE_PING,
    ATLAS_BENCH_RADIO_ID,
    ATLAS_BENCH_RADIO_PING,
    ATLAS_BENCH_UART_TEST,
    ATLAS_BENCH_SPI_TEST,
    ATLAS_BENCH_I2C_READ
} AtlasBenchOperation;
/** @brief Copied command: nonzero host ID, allowlisted operation, bounded integers. */
typedef struct
{
    uint32_t id;
    AtlasBenchOperation operation;
    uint32_t argument[6];
} AtlasBenchCommand;
/** @brief Stream framer; loss/overlength discards through the NEXT newline. */
typedef struct
{
    char line[ATLAS_BENCH_LINE_CAPACITY];
    size_t used;
    bool discard;
} AtlasBenchParser;
/** @brief JSON writer; once overflowed, the entire record must be discarded. */
typedef struct
{
    char *data;
    size_t capacity, used;
    bool ok;
} AtlasBenchJson;
/** @brief Clear a framer on connection change or known byte loss.
 * @param parser Destination. @param discard_until_lf Require a new line boundary. */
void AtlasBench_Reset(AtlasBenchParser *parser, bool discard_until_lf);
/** @brief Feed one byte; outputs a command only after a complete, valid line.
 * @param parser Framer. @param byte Input. @param command Output on return 1.
 * @return 1 valid command, -1 rejected completed line, 0 incomplete/blank. */
int AtlasBench_Feed(AtlasBenchParser *parser, uint8_t byte, AtlasBenchCommand *command);
/** @brief Parse a complete strict ASCII line: "ID verb arguments" (without LF).
 * @param line Bounded, NUL-terminated input. @param command Output on success.
 * @return true only for the entire allowlisted command, with no trailing tokens. */
bool AtlasBench_Parse(const char *line, AtlasBenchCommand *command);
/** @brief Start a bounded record. @param json Writer. @param data Buffer. @param capacity Bytes. */
void AtlasBench_JsonInit(AtlasBenchJson *json, char *data, size_t capacity);
/** @brief Append trusted JSON syntax, never module/user text. @param json Writer. @param text
 * Literal. */
void AtlasBench_JsonRaw(AtlasBenchJson *json, const char *text);
/** @brief Append escaped JSON string; non-ASCII is represented as a question mark.
 * @param json Writer. @param text Input. @param limit Maximum source bytes to inspect. */
void AtlasBench_JsonString(AtlasBenchJson *json, const char *text, size_t limit);
/** @brief Append unsigned decimal. @param json Writer. @param value Number. */
void AtlasBench_JsonU32(AtlasBenchJson *json, uint32_t value);
/** @brief Append signed decimal, including INT32_MIN. @param json Writer. @param value Number. */
void AtlasBench_JsonI32(AtlasBenchJson *json, int32_t value);
/** @brief Append rounded fixed-point integer or null for a nonfinite/out-of-range sample.
 * @param json Writer. @param value Measurement. @param scale Positive unit multiplier. */
void AtlasBench_JsonScaled(AtlasBenchJson *json, float value, uint32_t scale);
#endif
