/**
 * @file main.h
 * @brief Minimal STM32 HAL surface for host-side protocol and math tests.
 *
 * Major definitions:
 * - HAL handle stand-ins used by project-owned driver headers.
 * - Function prototypes implemented by test_hal_stubs.c.
 * - No-op barrier/timer macros used only by deterministic host tests.
 */

#ifndef ATLAS_TEST_MOCK_MAIN_H
#define ATLAS_TEST_MOCK_MAIN_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    HAL_OK = 0,
    HAL_ERROR,
    HAL_BUSY,
    HAL_TIMEOUT
} HAL_StatusTypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct GPIO_TypeDef GPIO_TypeDef;
struct GPIO_TypeDef { uint32_t unused; };

extern GPIO_TypeDef atlas_test_gpio_d;
extern GPIO_TypeDef atlas_test_gpio_e;
extern GPIO_TypeDef atlas_test_gpio_g;
extern GPIO_TypeDef atlas_test_gpio_b;

typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

typedef struct { uint32_t ErrorCode; } I2C_HandleTypeDef;
typedef struct { uint32_t unused; } SPI_HandleTypeDef;

typedef struct
{
    uint32_t BaudRate;
} UART_InitTypeDef;

typedef struct
{
    void *Instance;
    UART_InitTypeDef Init;
    uint32_t ErrorCode;
} UART_HandleTypeDef;

typedef struct
{
    uint32_t Prescaler;
} TIM_Base_InitTypeDef;

typedef struct { uint32_t CR1; } TIM_TypeDef;
typedef enum
{
    HAL_TIM_STATE_RESET = 0U,
    HAL_TIM_STATE_READY = 1U,
    HAL_TIM_STATE_BUSY = 2U
} HAL_TIM_StateTypeDef;

typedef struct
{
    TIM_TypeDef *Instance;
    TIM_Base_InitTypeDef Init;
    HAL_TIM_StateTypeDef State;
    uint32_t Channel;
    uint32_t counter;
    uint32_t capture;
    uint32_t autoreload;
} TIM_HandleTypeDef;

typedef struct
{
    uint32_t APB2CLKDivider;
} RCC_ClkInitTypeDef;

typedef struct
{
    uint32_t OCMode;
    uint32_t Pulse;
    uint32_t OCPolarity;
    uint32_t OCNPolarity;
    uint32_t OCFastMode;
    uint32_t OCIdleState;
    uint32_t OCNIdleState;
} TIM_OC_InitTypeDef;

extern TIM_TypeDef atlas_test_tim15_instance;

/** @brief Optional callback invoked by the host UART transmit stub. */
typedef void (*AtlasTestUartTransmitHook)(UART_HandleTypeDef *uart,
                                          const uint8_t *data,
                                          uint16_t length);
/** @brief Optional callback invoked by the host GPIO write stub. */
typedef void (*AtlasTestGpioWriteHook)(GPIO_TypeDef *port,
                                       uint16_t pin,
                                       GPIO_PinState state);
/** @brief Optional callback invoked by the host GPIO input stub. */
typedef GPIO_PinState (*AtlasTestGpioReadHook)(GPIO_TypeDef *port,
                                               uint16_t pin);
/** @brief Optional callback invoked by the host SPI transfer stub. */
typedef HAL_StatusTypeDef (*AtlasTestSpiTransferHook)(SPI_HandleTypeDef *spi,
                                                      uint8_t *tx,
                                                      uint8_t *rx,
                                                      uint16_t length,
                                                      uint32_t timeout_ms);
/** @brief Optional callbacks invoked by host I2C transfer stubs. */
typedef HAL_StatusTypeDef (*AtlasTestI2cTransmitHook)(I2C_HandleTypeDef *i2c,
                                                      uint16_t address,
                                                      uint8_t *data,
                                                      uint16_t length,
                                                      uint32_t timeout_ms);
typedef HAL_StatusTypeDef (*AtlasTestI2cReceiveHook)(I2C_HandleTypeDef *i2c,
                                                     uint16_t address,
                                                     uint8_t *data,
                                                     uint16_t length,
                                                     uint32_t timeout_ms);

#define TIM_CHANNEL_1               (1U)
#define TIM_CR1_CEN                 (1U)
#define TIM_CHANNEL_2               (2U)
#define HAL_TIM_ACTIVE_CHANNEL_1    (1U)
#define TIM15                       (&atlas_test_tim15_instance)
#define TIM_OCMODE_PWM1             (1U)
#define TIM_OCMODE_PWM2             (2U)
#define TIM_OCPOLARITY_HIGH         (1U)
#define TIM_OCNPOLARITY_HIGH        (1U)
#define TIM_OCFAST_DISABLE          (0U)
#define TIM_OCIDLESTATE_RESET       (0U)
#define TIM_OCNIDLESTATE_RESET      (0U)
#define RCC_HCLK_DIV1               (1U)
#define RCC_HCLK_DIV2               (2U)
#define UART_TXFIFO_THRESHOLD_1_8   (0U)
#define UART_RXFIFO_THRESHOLD_1_8   (0U)
#define I2C_ANALOGFILTER_ENABLE      (1U)
#define GPIO_PIN_0                   (UINT16_C(1) << 0)
#define GPIO_PIN_1                   (UINT16_C(1) << 1)
#define GPIO_PIN_4                   (UINT16_C(1) << 4)
#define GPIO_PIN_5                   (UINT16_C(1) << 5)
#define GPIO_PIN_6                   (UINT16_C(1) << 6)
#define GPIO_PIN_7                   (UINT16_C(1) << 7)
#define GPIO_PIN_13                  (UINT16_C(1) << 13)
#define GPIO_PIN_14                  (UINT16_C(1) << 14)
#define GPIO_MODE_OUTPUT_PP          (1U)
#define GPIO_NOPULL                  (0U)
#define GPIO_SPEED_FREQ_LOW          (0U)

#define BLE_SWITCH2_Pin              GPIO_PIN_4
#define BLE_SWITCH2_GPIO_Port        (&atlas_test_gpio_g)
#define BLE_SWITCH1_Pin              GPIO_PIN_5
#define BLE_SWITCH1_GPIO_Port        (&atlas_test_gpio_g)
#define BLE_DSR_Pin                  GPIO_PIN_6
#define BLE_DSR_GPIO_Port            (&atlas_test_gpio_g)
#define BLE_RESET_N_Pin              GPIO_PIN_4
#define BLE_RESET_N_GPIO_Port        (&atlas_test_gpio_d)
#define BLE_DTR_Pin                  GPIO_PIN_1
#define BLE_DTR_GPIO_Port            (&atlas_test_gpio_e)

#define LED_R_Pin                    GPIO_PIN_6
#define LED_R_GPIO_Port              (&atlas_test_gpio_b)
#define LED_G_Pin                    GPIO_PIN_7
#define LED_G_GPIO_Port              (&atlas_test_gpio_b)
#define LED_B_Pin                    GPIO_PIN_14
#define LED_B_GPIO_Port              (&atlas_test_gpio_d)

#define BNO085_H_INTN_Pin            GPIO_PIN_0
#define BNO085_H_INTN_GPIO_Port      (&atlas_test_gpio_g)
#define BNO085_NRST_Pin              GPIO_PIN_13
#define BNO085_NRST_GPIO_Port        (&atlas_test_gpio_b)

#define __DMB()                     do { } while (0)
#define __disable_irq()             do { } while (0)
#define __enable_irq()              do { } while (0)
#define __weak                      __attribute__((weak))
#define __HAL_TIM_GET_COUNTER(htim) ((htim)->counter)
#define __HAL_TIM_SET_COUNTER(htim, value) ((htim)->counter = (value))
#define __HAL_TIM_SET_AUTORELOAD(htim, value) ((htim)->autoreload = (value))

uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *configuration);
void AtlasTest_ResetGpioTrace(void);
uint32_t AtlasTest_GetOutputPins(GPIO_TypeDef *port);
uint32_t AtlasTest_GetHighPins(GPIO_TypeDef *port);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c,
                                          uint16_t address,
                                          uint8_t *data,
                                          uint16_t length,
                                          uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c,
                                         uint16_t address,
                                         uint8_t *data,
                                         uint16_t length,
                                         uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_DeInit(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *i2c,
                                                uint32_t enable);
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *i2c,
                                                 uint32_t coefficient);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,
                                          uint8_t *tx,
                                          uint8_t *rx,
                                          uint16_t length,
                                          uint32_t timeout_ms);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_IT(UART_HandleTypeDef *uart,
                                              uint8_t *data,
                                              uint16_t length);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart,
                                    uint8_t *data,
                                    uint16_t length,
                                    uint32_t timeout_ms);
HAL_StatusTypeDef HAL_UART_DeInit(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *uart);
HAL_StatusTypeDef HAL_UARTEx_SetTxFifoThreshold(UART_HandleTypeDef *uart,
                                                uint32_t threshold);
HAL_StatusTypeDef HAL_UARTEx_SetRxFifoThreshold(UART_HandleTypeDef *uart,
                                                uint32_t threshold);
HAL_StatusTypeDef HAL_UARTEx_EnableFifoMode(UART_HandleTypeDef *uart);
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size);
HAL_StatusTypeDef HAL_TIM_Base_Start(TIM_HandleTypeDef *timer);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel);
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *timer,
                                            TIM_OC_InitTypeDef *configuration,
                                            uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
uint32_t HAL_RCC_GetPCLK2Freq(void);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *clocks, uint32_t *flash_latency);
void AtlasTest_ResetTimerTrace(void);
uint32_t AtlasTest_GetTimerMode(uint32_t channel);
uint32_t AtlasTest_GetTimerStartedMask(void);
void AtlasTest_SetUartTransmitHook(AtlasTestUartTransmitHook hook);
void AtlasTest_ResetUartReceiveTrace(void);
void AtlasTest_SetUartStaleReceive(bool stale);
void AtlasTest_SetUartArmFailures(uint32_t failures);
uint32_t AtlasTest_GetUartAbortCount(void);
uint32_t AtlasTest_GetUartArmCount(void);
void AtlasTest_SetGpioWriteHook(AtlasTestGpioWriteHook hook);
void AtlasTest_SetGpioReadHook(AtlasTestGpioReadHook hook);
void AtlasTest_SetSpiTransferHook(AtlasTestSpiTransferHook hook);
void AtlasTest_SetI2cHooks(AtlasTestI2cTransmitHook transmit_hook,
                           AtlasTestI2cReceiveHook receive_hook);
void AtlasTest_ResetI2cTrace(void);
uint32_t AtlasTest_GetI2cDeinitCount(void);
uint32_t AtlasTest_GetI2cInitCount(void);
uint32_t AtlasTest_GetI2cAnalogFilterCount(void);
uint32_t AtlasTest_GetI2cDigitalFilterCount(void);
uint32_t AtlasTest_GetI2cDeinitWhileBnoResetCount(void);

#endif /* ATLAS_TEST_MOCK_MAIN_H */
