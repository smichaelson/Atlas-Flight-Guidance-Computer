/**
 * @file test_hal_stubs.c
 * @brief Deterministic, side-effect-free HAL stand-ins for host protocol tests.
 *
 * Major functions:
 * - HAL_GetTick()/HAL_Delay(): provide a controllable monotonic test clock.
 * - HAL transport stubs: satisfy driver linkage without touching real hardware.
 * - HAL timer stubs: expose deterministic PPS values to tests.
 */

#include "main.h"

#include <string.h>

static uint32_t atlas_test_tick;
static AtlasTestUartTransmitHook atlas_test_uart_transmit_hook;
static AtlasTestGpioWriteHook atlas_test_gpio_write_hook;
static AtlasTestSpiTransferHook atlas_test_spi_transfer_hook;
static AtlasTestI2cTransmitHook atlas_test_i2c_transmit_hook;
static AtlasTestI2cReceiveHook atlas_test_i2c_receive_hook;
GPIO_TypeDef atlas_test_gpio_d;
GPIO_TypeDef atlas_test_gpio_e;
GPIO_TypeDef atlas_test_gpio_g;
GPIO_TypeDef atlas_test_gpio_b;
static uint32_t atlas_test_output_b;
static uint32_t atlas_test_output_d;
static uint32_t atlas_test_output_e;
static uint32_t atlas_test_output_g;
static uint32_t atlas_test_high_b;
static uint32_t atlas_test_high_d;
static uint32_t atlas_test_high_e;
static uint32_t atlas_test_high_g;
uint32_t atlas_test_tim15_instance;
static uint32_t atlas_test_timer_mode_ch1;
static uint32_t atlas_test_timer_mode_ch2;
static uint32_t atlas_test_timer_started_mask;

/**
 * @brief Resolve a GPIO trace word for one mock port.
 * @param port Mock GPIO port.
 * @param b Storage for GPIOB.
 * @param d Storage for GPIOD.
 * @param e Storage for GPIOE.
 * @param g Storage for GPIOG.
 * @return Matching storage, or NULL for an unknown port.
 */
static uint32_t *atlas_test_gpio_word(GPIO_TypeDef *port,
                                      uint32_t *b,
                                      uint32_t *d,
                                      uint32_t *e,
                                      uint32_t *g)
{
    if (port == &atlas_test_gpio_b) { return b; }
    if (port == &atlas_test_gpio_d) { return d; }
    if (port == &atlas_test_gpio_e) { return e; }
    if (port == &atlas_test_gpio_g) { return g; }
    return NULL;
}

/** @brief Clear recorded GPIO modes and output levels. */
void AtlasTest_ResetGpioTrace(void)
{
    atlas_test_output_b = atlas_test_output_d = 0U;
    atlas_test_output_e = atlas_test_output_g = 0U;
    atlas_test_high_b = atlas_test_high_d = 0U;
    atlas_test_high_e = atlas_test_high_g = 0U;
}

/** @brief Return pins most recently configured as outputs on one mock port. */
uint32_t AtlasTest_GetOutputPins(GPIO_TypeDef *port)
{
    uint32_t *word = atlas_test_gpio_word(port, &atlas_test_output_b,
                                          &atlas_test_output_d,
                                          &atlas_test_output_e,
                                          &atlas_test_output_g);
    return (word == NULL) ? 0U : *word;
}

/** @brief Return the recorded high-output mask for one mock port. */
uint32_t AtlasTest_GetHighPins(GPIO_TypeDef *port)
{
    uint32_t *word = atlas_test_gpio_word(port, &atlas_test_high_b,
                                          &atlas_test_high_d,
                                          &atlas_test_high_e,
                                          &atlas_test_high_g);
    return (word == NULL) ? 0U : *word;
}

/**
 * @brief Install or clear the deterministic UART transmit response hook.
 * @param hook Callback, or NULL to disable injection.
 */
void AtlasTest_SetUartTransmitHook(AtlasTestUartTransmitHook hook)
{
    atlas_test_uart_transmit_hook = hook;
}

/**
 * @brief Install or clear the deterministic GPIO transition hook.
 * @param hook Callback, or NULL to disable injection.
 */
void AtlasTest_SetGpioWriteHook(AtlasTestGpioWriteHook hook)
{
    atlas_test_gpio_write_hook = hook;
}

/**
 * @brief Install or clear the deterministic SPI device hook.
 * @param hook Callback, or NULL to restore the fail-closed stub.
 */
void AtlasTest_SetSpiTransferHook(AtlasTestSpiTransferHook hook)
{
    atlas_test_spi_transfer_hook = hook;
}

/**
 * @brief Install or clear deterministic I2C device hooks.
 * @param transmit_hook Write callback, or NULL to fail closed.
 * @param receive_hook Read callback, or NULL to fail closed.
 */
void AtlasTest_SetI2cHooks(AtlasTestI2cTransmitHook transmit_hook,
                           AtlasTestI2cReceiveHook receive_hook)
{
    atlas_test_i2c_transmit_hook = transmit_hook;
    atlas_test_i2c_receive_hook = receive_hook;
}

/** @brief Return the deterministic host-test millisecond clock. */
uint32_t HAL_GetTick(void) { return atlas_test_tick; }

/** @brief Advance the deterministic host-test clock. @param delay_ms Milliseconds. */
void HAL_Delay(uint32_t delay_ms) { atlas_test_tick += delay_ms; }

/** @brief Record a GPIO output write in host tests. */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    uint32_t *word = atlas_test_gpio_word(port, &atlas_test_high_b,
                                          &atlas_test_high_d,
                                          &atlas_test_high_e,
                                          &atlas_test_high_g);
    if (word != NULL)
    {
        if (state == GPIO_PIN_SET) { *word |= pin; }
        else { *word &= ~(uint32_t)pin; }
    }
    if (atlas_test_gpio_write_hook != NULL)
    {
        atlas_test_gpio_write_hook(port, pin, state);
    }
}

/** @brief Record output-mode ownership established by project firmware. */
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *configuration)
{
    uint32_t *word = atlas_test_gpio_word(port, &atlas_test_output_b,
                                          &atlas_test_output_d,
                                          &atlas_test_output_e,
                                          &atlas_test_output_g);
    if ((word != NULL) && (configuration != NULL) &&
        (configuration->Mode == GPIO_MODE_OUTPUT_PP))
    {
        *word |= configuration->Pin;
    }
}

/** @brief Return inactive-high for unmodeled input pins. */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{ (void)port; (void)pin; return GPIO_PIN_SET; }

/** @brief Dispatch modeled I2C writes and reject all unmodeled transfers. */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c, uint16_t address,
                                          uint8_t *data, uint16_t length,
                                          uint32_t timeout_ms)
{
    if (atlas_test_i2c_transmit_hook != NULL)
    {
        return atlas_test_i2c_transmit_hook(i2c, address, data, length, timeout_ms);
    }
    (void)i2c; (void)address; (void)data; (void)length; (void)timeout_ms;
    return HAL_ERROR;
}

/** @brief Dispatch modeled I2C reads and reject all unmodeled transfers. */
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c, uint16_t address,
                                         uint8_t *data, uint16_t length,
                                         uint32_t timeout_ms)
{
    if (atlas_test_i2c_receive_hook != NULL)
    {
        return atlas_test_i2c_receive_hook(i2c, address, data, length, timeout_ms);
    }
    (void)i2c; (void)address; (void)data; (void)length; (void)timeout_ms;
    return HAL_ERROR;
}

/** @brief Dispatch modeled SPI exchanges and reject all unmodeled transfers. */
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi, uint8_t *tx,
                                          uint8_t *rx, uint16_t length,
                                          uint32_t timeout_ms)
{
    if (atlas_test_spi_transfer_hook != NULL)
    {
        return atlas_test_spi_transfer_hook(spi, tx, rx, length, timeout_ms);
    }
    (void)spi; (void)tx; (void)rx; (void)length; (void)timeout_ms;
    return HAL_ERROR;
}

/** @brief Accept receive arming in host transport tests. */
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *uart,
                                              uint8_t *data, uint16_t length)
{ (void)uart; (void)data; (void)length; return HAL_OK; }

/** @brief Accept host receive abort. */
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{ (void)uart; return HAL_OK; }

/** @brief Accept host transmission without loopback. */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                    uint16_t length, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (atlas_test_uart_transmit_hook != NULL)
    {
        atlas_test_uart_transmit_hook(uart, data, length);
    }
    return HAL_OK;
}

/** @brief Accept host UART deinitialization. */
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *uart)
{ (void)uart; return HAL_OK; }

/** @brief Accept host UART initialization. */
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *uart)
{ (void)uart; return HAL_OK; }

/** @brief Accept host FIFO threshold configuration. */
HAL_StatusTypeDef HAL_UARTEx_SetTxFifoThreshold(UART_HandleTypeDef *uart,
                                                uint32_t threshold)
{ (void)uart; (void)threshold; return HAL_OK; }

/** @brief Accept host FIFO threshold configuration. */
HAL_StatusTypeDef HAL_UARTEx_SetRxFifoThreshold(UART_HandleTypeDef *uart,
                                                uint32_t threshold)
{ (void)uart; (void)threshold; return HAL_OK; }

/** @brief Accept host FIFO enable. */
HAL_StatusTypeDef HAL_UARTEx_EnableFifoMode(UART_HandleTypeDef *uart)
{ (void)uart; return HAL_OK; }

/** @brief Accept host timer start. */
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer)
{ (void)timer; return HAL_OK; }

/** @brief Accept host input-capture start. */
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{ (void)timer; (void)channel; return HAL_OK; }

/** @brief Return the deterministic capture field. */
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel)
{ (void)channel; return timer->capture; }

/** @brief Reset PWM configuration/start traces. */
void AtlasTest_ResetTimerTrace(void)
{
    atlas_test_timer_mode_ch1 = 0U;
    atlas_test_timer_mode_ch2 = 0U;
    atlas_test_timer_started_mask = 0U;
}

/** @brief Return the last PWM mode configured for a channel. */
uint32_t AtlasTest_GetTimerMode(uint32_t channel)
{
    return (channel == TIM_CHANNEL_1) ? atlas_test_timer_mode_ch1 :
           (channel == TIM_CHANNEL_2) ? atlas_test_timer_mode_ch2 : 0U;
}

/** @brief Return the bitmask of currently started mock PWM channels. */
uint32_t AtlasTest_GetTimerStartedMask(void)
{
    return atlas_test_timer_started_mask;
}

/** @brief Record one PWM channel configuration. */
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *timer,
                                            TIM_OC_InitTypeDef *configuration,
                                            uint32_t channel)
{
    (void)timer;
    if (configuration == NULL) { return HAL_ERROR; }
    if (channel == TIM_CHANNEL_1) { atlas_test_timer_mode_ch1 = configuration->OCMode; }
    else if (channel == TIM_CHANNEL_2) { atlas_test_timer_mode_ch2 = configuration->OCMode; }
    else { return HAL_ERROR; }
    return HAL_OK;
}

/** @brief Mark one mock PWM channel started. */
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    if ((channel != TIM_CHANNEL_1) && (channel != TIM_CHANNEL_2)) { return HAL_ERROR; }
    atlas_test_timer_started_mask |= (1UL << channel);
    return HAL_OK;
}

/** @brief Mark one mock PWM channel stopped. */
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    if ((channel != TIM_CHANNEL_1) && (channel != TIM_CHANNEL_2)) { return HAL_ERROR; }
    atlas_test_timer_started_mask &= ~(1UL << channel);
    return HAL_OK;
}

/** @brief Return the generated 50 MHz APB2 peripheral clock. */
uint32_t HAL_RCC_GetPCLK2Freq(void) { return 50000000UL; }

/** @brief Return the generated APB2 divide-by-two clock configuration. */
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *clocks, uint32_t *flash_latency)
{
    if (clocks != NULL) { clocks->APB2CLKDivider = RCC_HCLK_DIV2; }
    if (flash_latency != NULL) { *flash_latency = 0U; }
}
