/**
 * @file atlas_bringup_protocol.c
 * @brief Strict, hardware-independent command decoder and bounded JSON writer.
 * Major functions: Feed handles fragmented/overlong lines; Parse implements a
 * finite command allowlist; Json* serialize integers and escaped diagnostic text.
 */
#include "atlas_bringup_protocol.h"
#include <limits.h>
#include <math.h>
#include <string.h>

/** @brief Consume one unsigned decimal without overflow or signs.
 * @param token NUL-terminated token. @param value Output. @return Valid integer. */
static bool bench_number(const char *token, uint32_t *value)
{
    if (token == NULL || *token == '\0')
        return false;
    uint32_t n = 0U;
    for (size_t i = 0U; token[i] != '\0'; ++i)
    {
        if (token[i] < '0' || token[i] > '9')
            return false;
        const uint32_t digit = (uint32_t)(token[i] - '0');
        if (n > (UINT32_MAX - digit) / 10U)
            return false;
        n = n * 10U + digit;
    }
    *value = n;
    return true;
}

/** @brief Parse a complete allowlisted command with exact arity.
 * @param line NUL-terminated input. @param command Destination. @return Validity. */
bool AtlasBench_Parse(const char *line, AtlasBenchCommand *command)
{
    char copy[ATLAS_BENCH_LINE_CAPACITY];
    char *token[8];
    size_t length = 0U, count = 0U;
    if (line == NULL || command == NULL)
        return false;
    while (length < sizeof(copy) && line[length] != '\0')
        ++length;
    if (length == 0U || length >= sizeof(copy))
        return false;
    memcpy(copy, line, length + 1U);
    for (size_t i = 0U; i < length;)
    {
        if (copy[i] == ' ')
        {
            copy[i++] = '\0';
            continue;
        }
        if (count == sizeof(token) / sizeof(token[0]))
            return false;
        token[count++] = &copy[i];
        while (i < length && copy[i] != ' ')
        {
            if (copy[i] < 0x21 || copy[i] > 0x7E)
                return false;
            ++i;
        }
    }
    AtlasBenchCommand parsed = {0};
    if (count < 2U || !bench_number(token[0], &parsed.id) || parsed.id == 0U)
        return false;
    struct BenchWord
    {
        const char *word;
        AtlasBenchOperation operation;
    };
    static const struct BenchWord simple[] = {
        {"hello", ATLAS_BENCH_HELLO},    {"status", ATLAS_BENCH_STATUS},
        {"beep", ATLAS_BENCH_BEEP},      {"stop", ATLAS_BENCH_STOP},
        {"uart", ATLAS_BENCH_UART_TEST}, {"spi", ATLAS_BENCH_SPI_TEST}};
    if (count == 2U)
    {
        for (size_t i = 0U; i < sizeof(simple) / sizeof(simple[0]); ++i)
            if (strcmp(token[1], simple[i].word) == 0)
            {
                parsed.operation = simple[i].operation;
                *command = parsed;
                return true;
            }
        return false;
    }
    if (strcmp(token[1], "probe") == 0 && count == 3U)
    {
        static const char *const devices[] = {"adxl", "lsm",  "mmc", "baro",
                                              "bno",  "gnss", "ble", "radio"};
        for (size_t i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
            if (strcmp(token[2], devices[i]) == 0)
            {
                parsed.operation = ATLAS_BENCH_PROBE;
                parsed.argument[0] = (uint32_t)i;
                *command = parsed;
                return true;
            }
        return false;
    }
    if (count == 3U && (strcmp(token[1], "led") == 0 || strcmp(token[1], "gpio") == 0))
    {
        if (!bench_number(token[2], &parsed.argument[0]) || parsed.argument[0] > 7U)
            return false;
        /* Rev-0.1 RGB output is hardware-inhibited; retain only the explicit
         * fail-dark request in the wire allowlist. */
        if (strcmp(token[1], "led") == 0 && parsed.argument[0] != 0U)
            return false;
        parsed.operation = strcmp(token[1], "led") == 0 ? ATLAS_BENCH_LED : ATLAS_BENCH_GPIO;
        /* GPIO zero is unconditional deassertion of all seven logic outputs. */
        *command = parsed;
        return true;
    }
    if (count == 3U)
    {
        struct BenchPair
        {
            const char *verb, *word;
            AtlasBenchOperation operation;
        };
        static const struct BenchPair pairs[] = {
            {"sd", "mount", ATLAS_BENCH_SD_MOUNT},       {"sd", "read", ATLAS_BENCH_SD_READ},
            {"sd", "test", ATLAS_BENCH_SD_TEST},         {"sd", "unmount", ATLAS_BENCH_SD_UNMOUNT},
            {"ble", "profile", ATLAS_BENCH_BLE_PROFILE}, {"ble", "data", ATLAS_BENCH_BLE_DATA},
            {"ble", "command", ATLAS_BENCH_BLE_COMMAND}, {"ble", "ping", ATLAS_BENCH_BLE_PING},
            {"radio", "id", ATLAS_BENCH_RADIO_ID},       {"radio", "ping", ATLAS_BENCH_RADIO_PING}};
        for (size_t i = 0U; i < sizeof(pairs) / sizeof(pairs[0]); ++i)
            if (strcmp(token[1], pairs[i].verb) == 0 && strcmp(token[2], pairs[i].word) == 0)
            {
                parsed.operation = pairs[i].operation;
                *command = parsed;
                return true;
            }
    }
    if (strcmp(token[1], "utc") == 0 && count == 8U)
    {
        const uint32_t minimum[] = {2000U, 1U, 1U, 0U, 0U, 0U};
        const uint32_t maximum[] = {2099U, 12U, 31U, 23U, 59U, 59U};
        for (size_t i = 0U; i < 6U; ++i)
            if (!bench_number(token[i + 2U], &parsed.argument[i]) ||
                parsed.argument[i] < minimum[i] || parsed.argument[i] > maximum[i])
                return false;
        /* The storage owner additionally rejects impossible dates (e.g. February 30). */
        parsed.operation = ATLAS_BENCH_UTC;
        *command = parsed;
        return true;
    }
    if (strcmp(token[1], "i2c") == 0 && count == 4U &&
        bench_number(token[2], &parsed.argument[0]) &&
        bench_number(token[3], &parsed.argument[1]) && parsed.argument[0] >= 8U &&
        parsed.argument[0] <= 119U && parsed.argument[1] <= 255U)
    {
        parsed.operation = ATLAS_BENCH_I2C_READ;
        *command = parsed;
        return true;
    }
    return false;
}

/** @brief Reset framing after a connection boundary or known byte loss.
 * @param parser Destination. @param discard_until_lf Discard until newline. */
void AtlasBench_Reset(AtlasBenchParser *parser, bool discard_until_lf)
{
    if (parser != NULL)
    {
        memset(parser, 0, sizeof(*parser));
        parser->discard = discard_until_lf;
    }
}

/** @brief Incrementally frame commands without accepting a truncated suffix.
 * @param parser State. @param byte Next byte. @param command Parsed output.
 * @return 1 complete command, -1 rejected line, 0 no command. */
int AtlasBench_Feed(AtlasBenchParser *parser, uint8_t byte, AtlasBenchCommand *command)
{
    if (parser == NULL || command == NULL)
        return -1;
    if (byte == '\n')
    {
        int result = 0;
        if (parser->discard)
            result = -1;
        else if (parser->used != 0U)
        {
            if (parser->line[parser->used - 1U] == '\r')
                --parser->used;
            parser->line[parser->used] = '\0';
            result = AtlasBench_Parse(parser->line, command) ? 1 : -1;
        }
        AtlasBench_Reset(parser, false);
        return result;
    }
    if (parser->discard)
        return 0;
    if (parser->used + 1U >= sizeof(parser->line) ||
        (byte != '\r' && (byte < 0x20U || byte > 0x7EU)))
    {
        parser->discard = true;
        return 0;
    }
    parser->line[parser->used++] = (char)byte;
    return 0;
}

/** @brief Initialize a writer and its terminating NUL.
 * @param json Writer. @param data Buffer. @param capacity Total bytes. */
void AtlasBench_JsonInit(AtlasBenchJson *json, char *data, size_t capacity)
{
    if (json == NULL)
        return;
    *json = (AtlasBenchJson){data, capacity, 0U, data != NULL && capacity != 0U};
    if (json->ok)
        data[0] = '\0';
}
/** @brief Append one checked byte. @param json Writer. @param byte Byte. */
static void bench_json_byte(AtlasBenchJson *json, char byte)
{
    if (json == NULL || !json->ok)
        return;
    if (json->used + 1U >= json->capacity)
    {
        json->ok = false;
        return;
    }
    json->data[json->used++] = byte;
    json->data[json->used] = '\0';
}
/** @brief Append trusted syntax. @param json Writer. @param text Literal. */
void AtlasBench_JsonRaw(AtlasBenchJson *json, const char *text)
{
    if (text != NULL)
        for (size_t i = 0U; text[i] != '\0'; ++i)
            bench_json_byte(json, text[i]);
}
/** @brief Quote and escape bounded external text.
 * @param json Writer. @param text Input. @param limit Maximum bytes inspected. */
void AtlasBench_JsonString(AtlasBenchJson *json, const char *text, size_t limit)
{
    static const char hex[] = "0123456789abcdef";
    bench_json_byte(json, '"');
    for (size_t i = 0U; text != NULL && i < limit && text[i] != '\0'; ++i)
    {
        const unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\')
        {
            bench_json_byte(json, '\\');
            bench_json_byte(json, (char)c);
        }
        else if (c < 0x20U)
        {
            AtlasBench_JsonRaw(json, "\\u00");
            bench_json_byte(json, hex[c >> 4]);
            bench_json_byte(json, hex[c & 15U]);
        }
        else
            bench_json_byte(json, c <= 0x7EU ? (char)c : '?');
    }
    bench_json_byte(json, '"');
}
/** @brief Serialize all uint32 values without libc. @param json Writer. @param value Number. */
void AtlasBench_JsonU32(AtlasBenchJson *json, uint32_t value)
{
    char reverse[10];
    size_t count = 0U;
    do
    {
        reverse[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    while (count != 0U)
        bench_json_byte(json, reverse[--count]);
}
/** @brief Serialize signed values without negating INT32_MIN.
 * @param json Writer. @param value Number. */
void AtlasBench_JsonI32(AtlasBenchJson *json, int32_t value)
{
    if (value < 0)
        bench_json_byte(json, '-');
    AtlasBench_JsonU32(json, value < 0 ? 0U - (uint32_t)value : (uint32_t)value);
}
/** @brief Scale a finite sample and round symmetrically; invalid values become null.
 * @param json Writer. @param value Sample. @param scale Unit multiplier. */
void AtlasBench_JsonScaled(AtlasBenchJson *json, float value, uint32_t scale)
{
    const double scaled = (double)value * (double)scale;
    const double rounded = scaled < 0.0 ? scaled - 0.5 : scaled + 0.5;
    /* C truncates toward zero. This open interval includes INT32_MIN's valid
     * -0.5 rounding adjustment without permitting an out-of-range conversion. */
    if (scale == 0U || !isfinite(rounded) || rounded <= (double)INT32_MIN - 1.0 ||
        rounded >= (double)INT32_MAX + 1.0)
        AtlasBench_JsonRaw(json, "null");
    else
        AtlasBench_JsonI32(json, (int32_t)rounded);
}
