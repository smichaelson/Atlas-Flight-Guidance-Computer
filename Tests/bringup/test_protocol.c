/** @file test_protocol.c @brief Pure bench framing/serialization adversarial tests.
 * Major functions: main checks strict commands, byte loss, bounded writes,
 * integer extrema and nonfinite data. No hardware or serial port is accessed. */
#include "atlas_bringup_protocol.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/** @brief Feed one complete test string. @return Last parser result. */
static int feed(AtlasBenchParser *p, const char *text, AtlasBenchCommand *command)
{
    int result = 0;
    while (*text)
        result = AtlasBench_Feed(p, (uint8_t)*text++, command);
    return result;
}

/** @brief Exercise the protocol without any driver mocks. @return Zero on success. */
int main(void)
{
    AtlasBenchCommand c;
    const char *valid[] = {"1 hello",
                           "2 status",
                           "3 beep",
                           "4 stop",
                           "5 led 0",
                           "6 led 7",
                           "7 gpio 0",
                           "8 gpio 7",
                           "9 probe adxl",
                           "10 probe lsm",
                           "11 probe mmc",
                           "12 probe baro",
                           "13 probe bno",
                           "14 probe gnss",
                           "15 probe ble",
                           "16 probe radio",
                           "17 sd mount",
                           "18 sd read",
                           "19 sd test",
                           "20 sd unmount",
                           "21 ble profile",
                           "22 ble data",
                           "23 ble command",
                           "24 ble ping",
                           "25 radio id",
                           "26 radio ping",
                           "27 uart",
                           "28 spi",
                           "29 i2c 8 0",
                           "30 i2c 119 255",
                           "4294967295 utc 2099 12 31 23 59 59"};
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
        assert(AtlasBench_Parse(valid[i], &c));
    const char *invalid[] = {"",
                             "0 hello",
                             "-1 hello",
                             "+1 hello",
                             "4294967296 hello",
                             "1.0 hello",
                             "1 HELLO",
                             "1 probe",
                             "1 probe unknown",
                             "1 sd",
                             "1 sd format",
                             "1 pyro fire",
                             "1 pwm 1",
                             "1 gpio 8",
                             "1 gpio -1",
                             "1 led 8",
                             "1 led 2 ignored",
                             "1 utc 1999 1 1 0 0 0",
                             "1 utc 2100 1 1 0 0 0",
                             "1 utc 2026 13 1 0 0 0",
                             "1 utc 2026 1 1 24 0 0",
                             "1 utc 2026 1 1 0 60 0",
                             "1 utc 2026 1 1 0 0 60",
                             "1 i2c 7 0",
                             "1 i2c 120 0",
                             "1 i2c 8 256",
                             "1 i2c 0x50 0",
                             "1\thello",
                             "1 he\rllo",
                             "1 hello\n2 beep",
                             "1 utc 2026 1 1 0 0 0 extra",
                             "1 arbitrary register write"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        assert(!AtlasBench_Parse(invalid[i], &c));
    assert(!AtlasBench_Parse(NULL, &c) && !AtlasBench_Parse("1 hello", NULL));
    AtlasBenchParser p;
    AtlasBench_Reset(&p, false);
    assert(feed(&p, "123 pro", &c) == 0 && feed(&p, "be adxl\r\n", &c) == 1 && c.id == 123U);
    for (unsigned i = 0; i < 500U; ++i)
        assert(AtlasBench_Feed(&p, 'x', &c) == 0);
    assert(feed(&p, "1 beep\n", &c) == -1); /* A truncated suffix is never executable. */
    assert(feed(&p, "2 status\n", &c) == 1 && c.id == 2U);
    AtlasBench_Reset(&p, true);
    assert(feed(&p, "3 beep\n", &c) == -1 && feed(&p, "4 hello\n", &c) == 1);
    assert(AtlasBench_Feed(&p, 0U, &c) == 0 && feed(&p, "5 beep\n", &c) == -1);
    uint32_t random = 1U;
    for (unsigned i = 0; i < 100000U; ++i)
    {
        random = random * 1664525U + 1013904223U;
        (void)AtlasBench_Feed(&p, (uint8_t)(random >> 24), &c);
        assert(p.used < ATLAS_BENCH_LINE_CAPACITY);
    }
    char output[256];
    AtlasBenchJson j;
    AtlasBench_JsonInit(&j, output, sizeof(output));
    AtlasBench_JsonU32(&j, UINT32_MAX);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonI32(&j, INT32_MIN);
    assert(j.ok && strcmp(output, "4294967295,-2147483648") == 0);
    AtlasBench_JsonInit(&j, output, sizeof(output));
    AtlasBench_JsonString(&j, "\"\\\n\t\x01\xFF", 6U);
    assert(j.ok && strcmp(output, "\"\\\"\\\\\\u000a\\u0009\\u0001?\"") == 0);
    AtlasBench_JsonInit(&j, output, sizeof(output));
    AtlasBench_JsonScaled(&j, -1.25f, 10U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, 1.25f, 10U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, NAN, 100U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, INFINITY, 1U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, 1e30f, 1000U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, 1.0f, 0U);
    assert(j.ok && strcmp(output, "-13,13,null,null,null,null") == 0);
    AtlasBench_JsonInit(&j, output, sizeof(output));
    AtlasBench_JsonScaled(&j, -2147483648.0f, 1U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, 2147483648.0f, 1U);
    AtlasBench_JsonRaw(&j, ",");
    AtlasBench_JsonScaled(&j, -2147483904.0f, 1U);
    /* Rounding is checked before the C conversion: INT32_MIN remains valid,
     * while neither positive overflow nor the next float below it is cast. */
    assert(j.ok && strcmp(output, "-2147483648,null,null") == 0);
    char bounded[5] = {'x', 'x', 'x', 'x', '!'};
    AtlasBench_JsonInit(&j, bounded, 4U);
    AtlasBench_JsonRaw(&j, "abcd");
    assert(!j.ok && bounded[3] == '\0' && bounded[4] == '!');
    AtlasBench_JsonU32(&j, UINT32_MAX);
    assert(bounded[4] == '!');
    AtlasBench_JsonInit(&j, NULL, 0U);
    AtlasBench_JsonRaw(&j, "no write");
    assert(!j.ok);
    puts("Bench protocol: allowlist, 100000 fuzz bytes, framing loss, JSON bounds/escaping/units "
         "PASS");
    return 0;
}
