/**
 * @file main.h
 * @brief Register/HAL boundary model for inert Atlas service tests.
 * Major definitions: GPIO, timer, DMA, ADC, RTC and safety-register snapshots.
 * Values model only fields used by the service; this is not a silicon emulator.
 */
#ifndef ATLAS_SERVICE_MAIN_H
#define ATLAS_SERVICE_MAIN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {HAL_OK=0,HAL_ERROR,HAL_BUSY,HAL_TIMEOUT} HAL_StatusTypeDef;
typedef enum {GPIO_PIN_RESET=0,GPIO_PIN_SET} GPIO_PinState;
typedef struct { uint32_t backup; } RTC_HandleTypeDef;
typedef struct { uint8_t Hours, Minutes, Seconds; uint32_t DayLightSaving, StoreOperation; } RTC_TimeTypeDef;
typedef struct { uint8_t WeekDay, Month, Date, Year; } RTC_DateTypeDef;
typedef struct { uint32_t LogBlockNbr, LogBlockSize; } HAL_SD_CardInfoTypeDef;
typedef struct { void *Instance; struct { uint32_t BaudRate; } Init; } UART_HandleTypeDef;
typedef struct { void *Instance; } I2C_HandleTypeDef;
typedef struct { void *Instance; } SPI_HandleTypeDef;
typedef struct { uint32_t MODER, BSRR, ODR, IDR; } GPIO_TypeDef;
typedef struct { uint32_t CR1, DIER, PSC, ARR, EGR, SR, CNT, CCER, BDTR, CCR[4]; } TIM_TypeDef;
typedef struct { TIM_TypeDef *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t CR; } DMA_Stream_TypeDef;
typedef struct DMA_HandleTypeDef DMA_HandleTypeDef;
struct DMA_HandleTypeDef {
    DMA_Stream_TypeDef *Instance;
    struct { uint32_t MemInc; } Init;
    uint32_t State;
    void (*XferCpltCallback)(DMA_HandleTypeDef *), (*XferHalfCpltCallback)(DMA_HandleTypeDef *);
    void (*XferErrorCallback)(DMA_HandleTypeDef *), (*XferAbortCallback)(DMA_HandleTypeDef *);
};
typedef struct { uint32_t ISR, raw, selected_channel; } ADC_TypeDef;
typedef struct { ADC_TypeDef *Instance; DMA_HandleTypeDef *DMA_Handle;
    struct { uint32_t ScanConvMode,NbrOfConversion,EOCSelection; } Init;
    uint32_t ErrorCode; } ADC_HandleTypeDef;
typedef struct { uint32_t Channel,Rank,SamplingTime,SingleDiff,OffsetNumber; } ADC_ChannelConfTypeDef;
typedef struct { uint32_t FADD; } RAMECC_MonitorTypeDef;
typedef struct { RAMECC_MonitorTypeDef *Instance; uint32_t RAMECCErrorCode; } RAMECC_HandleTypeDef;
typedef struct { uint32_t CCR; } TestScb;
typedef struct { uint32_t CFGR,RSR; } TestRcc;
extern GPIO_TypeDef test_gpio[7];
extern TIM_TypeDef test_tim[3];
extern DMA_Stream_TypeDef test_dma[2];
extern ADC_TypeDef test_adc[2];
extern RAMECC_MonitorTypeDef test_ecc[5];
extern TestScb test_scb;
extern TestRcc test_rcc;
extern uint16_t test_vref_cal, test_temp_cal1, test_temp_cal2;
extern uint32_t test_tick, test_primask, test_ipsr;
extern int test_scheduler;
extern bool test_permitted, test_abort_fails, test_safety_pending;
extern unsigned test_dma_launches, test_abort_calls;
#define GPIOA (&test_gpio[0])
#define GPIOB (&test_gpio[1])
#define GPIOC (&test_gpio[2])
#define GPIOD (&test_gpio[3])
#define GPIOE (&test_gpio[4])
#define GPIOF (&test_gpio[5])
#define GPIOG (&test_gpio[6])
#define TIM1 (&test_tim[0])
#define TIM3 (&test_tim[1])
#define TIM6 (&test_tim[2])
#define DMA1_Stream0 (&test_dma[0])
#define DMA1_Stream1 (&test_dma[1])
#define ADC1 (&test_adc[0])
#define ADC3 (&test_adc[1])
#define RAMECC1_Monitor3 (&test_ecc[1])
#define SCB (&test_scb)
#define RCC (&test_rcc)
#define SCB_CCR_DC_Msk (1U<<16)
#define RCC_CFGR_TIMPRE (1U<<15)
#define VREFINT_CAL_ADDR (&test_vref_cal)
#define TEMPSENSOR_CAL1_ADDR (&test_temp_cal1)
#define TEMPSENSOR_CAL2_ADDR (&test_temp_cal2)
#define TIM_CR1_CEN 1U
#define TIM_DIER_UDE (1U<<8)
#define TIM_EGR_UG 1U
#define TIM_BDTR_MOE (1U<<15)
#define DMA_SxCR_EN 1U
#define HAL_DMA_STATE_BUSY 2U
#define HAL_DMA_STATE_READY 1U
#define DMA_MINC_ENABLE (1U<<10)
#define ADC_FLAG_OVR 4U
#define ADC_FLAG_EOC 1U
#define ADC_RESOLUTION_16B 0U
#define ADC_CHANNEL_VREFINT 19U
#define ADC_CHANNEL_TEMPSENSOR 18U
#define ADC_REGULAR_RANK_1 1U
#define ADC_SAMPLETIME_810CYCLES_5 7U
#define ADC_SINGLE_ENDED 0U
#define ADC_OFFSET_NONE 0U
#define ADC_SCAN_DISABLE 0U
#define ADC_EOC_SINGLE_CONV 1U
#define ADC_CALIB_OFFSET_LINEARITY 1U
#define RAMECC_IT_MONITOR_ALL 7U
#define HAL_RAMECC_ERROR_NONE 0U
#define PWR_FLAG_PVDO 1U
#define CLEAR_BIT(reg,bits) ((reg) &= ~(bits))
#define SET_BIT(reg,bits) ((reg) |= (bits))
#define MODIFY_REG(reg,clear,set) ((reg) = ((reg) & ~(clear)) | (set))
#define __DMB() ((void)0)
#define __DSB() ((void)0)
#define __get_IPSR() (test_ipsr)
#define __get_PRIMASK() (test_primask)
#define __disable_irq() (test_primask = 1U)
void TestSetPrimask(uint32_t mask);
#define __set_PRIMASK(mask) TestSetPrimask(mask)
#define __HAL_DBGMCU_UnFreeze_TIM6() ((void)0)
#define __HAL_PWR_GET_FLAG(flag) (false)
#define __HAL_ADC_GET_FLAG(adc,flag) (((adc)->Instance->ISR & (flag)) != 0U)
#define __HAL_ADC_CALC_VREFANALOG_VOLTAGE(raw,res) ((uint32_t)test_vref_cal * 3300U / (raw))
#define __HAL_ADC_CALC_TEMPERATURE(vdda,raw,res) (30 + ((int32_t)(raw) - test_temp_cal1) / 100)
#define __HAL_TIM_SET_COMPARE(timer,channel,value) ((timer)->Instance->CCR[(channel)/4U] = (value))
#define PYRO_FIRE1_Pin (1U<<9)
#define PYRO_FIRE2_Pin (1U<<10)
#define PYRO_FIRE3_Pin (1U<<11)
#define PYRO_FIRE4_Pin (1U<<12)
#define PYRO_FIRE5_Pin (1U<<13)
#define GPIO_OUT1_Pin (1U<<2)
#define GPIO_OUT2_Pin (1U<<4)
#define GPIO_OUT3_Pin (1U<<5)
#define GPIO_OUT4_Pin (1U<<7)
#define GPIO_OUT5_Pin (1U<<5)
#define GPIO_OUT6_Pin (1U<<14)
#define GPIO_OUT7_Pin (1U<<13)
#define GPIO_IN1_Pin (1U<<2)
#define GPIO_IN2_Pin (1U<<3)
#define GPIO_IN3_Pin (1U<<1)
#define GPIO_IN4_Pin (1U<<7)
#define GPIO_IN5_Pin (1U<<8)
#define GPIO_IN6_Pin (1U<<10)
#define GPIO_IN7_Pin (1U<<15)
#define EXT_SWITCH_Pin (1U<<12)
#define EXT_SWITCH_GPIO_Port GPIOF
#define USB_VBUS_Pin (1U<<9)
#define USB_VBUS_GPIO_Port GPIOA
#define USB_OTG_FS ((void *)(uintptr_t)0x40080000U)
#define OTG_FS_IRQn 67
#define RTC_BKP_DR0 0U
#define RTC_FORMAT_BIN 0U
#define RTC_DAYLIGHTSAVING_NONE 0U
#define RTC_STOREOPERATION_RESET 0U
#define UART4 ((void *)(uintptr_t)0x40004C00U)
#define I2C2 ((void *)(uintptr_t)0x40005800U)
#define SPI3 ((void *)(uintptr_t)0x40003C00U)
#define I2C_MEMADD_SIZE_8BIT 1U
#define I2C_MEMADD_SIZE_16BIT 2U
#define HAL_I2C_ERROR_AF 4U
#define CS_SPI_EXT_Pin (1U<<13)
#define CS_SPI_EXT_GPIO_Port GPIOG
#define CS_LSM6DSV16B_Pin (1U<<10)
#define CS_LSM6DSV16B_GPIO_Port GPIOG
#define BNO085_H_INTN_Pin (1U<<0)
#define BNO085_H_INTN_GPIO_Port GPIOG
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *spi,uint8_t *tx,uint8_t *rx,uint16_t size,uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *i2c,uint16_t address,uint8_t *data,uint16_t size,uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *i2c,uint16_t address,uint8_t *data,uint16_t size,uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *i2c,uint16_t address,uint16_t reg,uint16_t reg_size,uint8_t *data,uint16_t size,uint32_t timeout);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *i2c,uint16_t address,uint16_t reg,uint16_t reg_size,uint8_t *data,uint16_t size,uint32_t timeout);
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *i2c);
HAL_StatusTypeDef USB_DevDisconnect(const void *instance);
void HAL_NVIC_DisableIRQ(int irq);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_RTCEx_BKUPWrite(RTC_HandleTypeDef *rtc,uint32_t index,uint32_t value);
uint32_t HAL_RTCEx_BKUPRead(RTC_HandleTypeDef *rtc,uint32_t index);
HAL_StatusTypeDef HAL_RTC_SetTime(RTC_HandleTypeDef *rtc,const RTC_TimeTypeDef *time,uint32_t format);
HAL_StatusTypeDef HAL_RTC_SetDate(RTC_HandleTypeDef *rtc,const RTC_DateTypeDef *date,uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetTime(RTC_HandleTypeDef *rtc,RTC_TimeTypeDef *time,uint32_t format);
HAL_StatusTypeDef HAL_RTC_GetDate(RTC_HandleTypeDef *rtc,RTC_DateTypeDef *date,uint32_t format);
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetPCLK2Freq(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma);
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma,uint32_t source,uint32_t target,uint32_t count);
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc,const ADC_ChannelConfTypeDef *channel);
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc,uint32_t timeout);
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,uint32_t *data,uint32_t length);
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc);
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc,uint32_t mode,uint32_t diff);
HAL_StatusTypeDef HAL_RAMECC_Init(RAMECC_HandleTypeDef *monitor);
HAL_StatusTypeDef HAL_RAMECC_StartMonitor(RAMECC_HandleTypeDef *monitor);
HAL_StatusTypeDef HAL_RAMECC_EnableNotification(RAMECC_HandleTypeDef *monitor,uint32_t flags);
uint32_t HAL_RAMECC_GetFailingAddress(RAMECC_HandleTypeDef *monitor);
void HAL_RAMECC_IRQHandler(RAMECC_HandleTypeDef *monitor);
#endif
