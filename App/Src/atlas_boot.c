/**
 * @file atlas_boot.c
 * @brief Reset-first STM32H743 factory DFU entry, with a consumed RAM marker.
 * Major functions: MarkerValid rejects accidental/reset-stale requests;
 * EarlyCheck branches before peripheral setup; RequestDfu performs SYSRESETREQ.
 * Reference: ST AN2606, STM32H74xxx/75xxx, vector table 0x1FF09800.
 */
#include "atlas_boot.h"

/** @brief Require an intentional software reset and complementary marker.
 * @param software Software-reset flag. @param power Power/brownout flag.
 * @param first Marker. @param second Complement. @return Validity. */
bool AtlasBoot_MarkerValid(bool software, bool power, uint32_t first, uint32_t second)
{
    return software && !power && first == ATLAS_BOOT_MAGIC && second == ~ATLAS_BOOT_MAGIC;
}

#ifndef ATLAS_BOOT_HOST_TEST
#include "stm32h7xx.h"
#define ATLAS_ROM_VECTOR 0x1FF09800U
#if defined(__ICCARM__)
#pragma location = ".atlas_boot"
#pragma data_alignment = 8
__no_init static volatile uint32_t boot_request[2];
#else
static volatile uint32_t boot_request[2]
    __attribute__((section(".atlas_boot"), aligned(8), used));
#endif

#if defined(__GNUC__)
/** @brief Set the ROM stack and branch without a compiler-generated epilogue.
 * @param stack ROM MSP. @param entry Thumb reset handler. */
__attribute__((naked, noreturn)) static void boot_branch(uint32_t stack, uint32_t entry)
{
    __asm volatile("msr msp, r0\n"
                   "cpsie i\n"
                   "bx r1\n");
}
#else
/* IAR assembly trampoline preserves register arguments across the MSP change. */
extern void AtlasBoot_Branch(uint32_t stack, uint32_t entry);
#endif

/** @brief Consume the marker once, then hand off from reset-default hardware. */
void AtlasBoot_EarlyCheck(void)
{
    const uint32_t flags = RCC->RSR;
    bool requested = false;
    /* Do not READ uninitialized SRAM/ECC on a cold boot. Every boot writes
     * the entire reserved marker before any later software reset can read it. */
    if ((flags & RCC_RSR_SFTRSTF) != 0U &&
        (flags & (RCC_RSR_PORRSTF | RCC_RSR_BORRSTF)) == 0U)
        requested = AtlasBoot_MarkerValid(true, false, boot_request[0], boot_request[1]);
    boot_request[0] = 0U;
    boot_request[1] = 0U;
    __DSB();
    if (!requested)
        return;
    const uint32_t stack = *(const uint32_t *)ATLAS_ROM_VECTOR;
    const uint32_t entry = *(const uint32_t *)(ATLAS_ROM_VECTOR + 4U);
    const bool rom_stack = (stack > 0x20000000U && stack <= 0x20020000U) ||
                           (stack > 0x24000000U && stack <= 0x24080000U);
    if ((stack & 7U) != 0U || !rom_stack ||
        (entry & 1U) == 0U || entry < 0x1FF00001U || entry >= 0x1FF20000U)
        return; /* Unsupported ROM vectors: retain normal recovery access. */
    __disable_irq();
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;
    for (unsigned i = 0U; i < 8U; ++i)
    {
        NVIC->ICER[i] = 0xFFFFFFFFU;
        NVIC->ICPR[i] = 0xFFFFFFFFU;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    SCB->VTOR = ATLAS_ROM_VECTOR;
    __set_CONTROL(0U);
    __set_BASEPRI(0U);
    __set_FAULTMASK(0U);
    __DSB();
    __ISB();
#if defined(__GNUC__)
    boot_branch(stack, entry);
#else
    AtlasBoot_Branch(stack, entry);
    for (;;) { }
#endif
}

/** @brief Store a one-shot request, clear accumulated causes, and reset. */
void AtlasBoot_RequestDfu(void)
{
    __disable_irq();
    boot_request[0] = ATLAS_BOOT_MAGIC;
    boot_request[1] = ~ATLAS_BOOT_MAGIC;
    RCC->RSR |= RCC_RSR_RMVF;
    __DSB();
    NVIC_SystemReset();
}
#endif
