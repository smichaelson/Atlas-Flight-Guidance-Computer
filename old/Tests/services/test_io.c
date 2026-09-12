/**
 * @file test_io.c
 * @brief Inert production-output-adapter tests against register/HAL boundaries.
 * Major functions: main checks qualification/expiry, DMA pulse setup and safe abort;
 * fixture supplies generated handles without pretending to boot a real STM32.
 * This includes the real module to inspect private ownership state. The timer/DMA
 * event model is NOT proof of silicon routing, pulse width or electrical safety.
 */
#include "../../App/Src/atlas_io.c"
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>

GPIO_TypeDef test_gpio[7];
TIM_TypeDef test_tim[3];
DMA_Stream_TypeDef test_dma[2];
ADC_TypeDef test_adc[2];
RAMECC_MonitorTypeDef test_ecc[5];
TestScb test_scb;
TestRcc test_rcc;
uint16_t test_vref_cal=24000U, test_temp_cal1=10000U, test_temp_cal2=20000U;
uint32_t test_tick, test_primask, test_ipsr;
int test_scheduler=taskSCHEDULER_RUNNING;
bool test_permitted, test_abort_fails, test_safety_pending;
unsigned test_dma_launches, test_abort_calls;
RAMECC_HandleTypeDef hramecc1_m1,hramecc1_m4,hramecc2_m1,hramecc3_m1;
static TIM_HandleTypeDef timers[3];
static DMA_HandleTypeDef dmas[2];
static ADC_HandleTypeDef adcs[2];
static jmp_buf owner_loop_exit;
static unsigned owner_cycles_remaining;
#if ATLAS_BRINGUP
static AtlasUsbHealth bench_test_usb;
bool AtlasUsb_GetHealth(AtlasUsbHealth *health) { *health=bench_test_usb;return true; }
#endif

/** @brief Simulate deferred safety IRQ delivery at the end of a register sequence.
 * @param mask Requested mock interrupt mask. */
void TestSetPrimask(uint32_t mask)
{
    test_primask=mask;
    if (!mask && test_safety_pending) { test_safety_pending=false; AtlasIo_EmergencyStop(); }
}
void AtlasService_CriticalEnter(void) { }
void AtlasService_CriticalExit(void) { }
bool AtlasRtos_OutputsPermitted(void) { return test_permitted; }
void AtlasRtos_InhibitOutputs(void) { test_permitted=false; AtlasIo_EmergencyStop(); }
uint32_t HAL_GetTick(void) { return test_tick; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 100000000U; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 50000000U; }
uint32_t HAL_RCC_GetPCLK2Freq(void) { return 50000000U; }
BaseType_t xTaskGetSchedulerState(void) { return test_scheduler; }
TickType_t xTaskGetTickCount(void) { return test_tick; }
void vTaskDelay(TickType_t ticks) { test_tick+=ticks; }
void vTaskDelayUntil(TickType_t *wake,TickType_t period)
{
    *wake+=period; test_tick=*wake;
    if (owner_cycles_remaining != 0U && --owner_cycles_remaining == 0U)
        longjmp(owner_loop_exit, 1);
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task) { (void)task; return 1000U; }
TaskHandle_t xTaskCreateStatic(void (*entry)(void *),const char *name,uint32_t words,void *argument,
    UBaseType_t priority,StackType_t *stack,StaticTask_t *control)
{ (void)entry;(void)name;(void)words;(void)argument;(void)priority;(void)stack;return control; }
QueueHandle_t xQueueCreateStatic(UBaseType_t capacity,UBaseType_t size,uint8_t *memory,StaticQueue_t *control)
{ *control=(StaticQueue_t){memory,size,capacity,0U,0U,0U};return control; }
BaseType_t xQueueSend(QueueHandle_t q,const void *item,TickType_t wait)
{ (void)wait;if(q->count==q->capacity)return pdFALSE;memcpy(q->memory+(q->head++%q->capacity)*q->size,item,q->size);++q->count;return pdTRUE; }
BaseType_t xQueuePeek(QueueHandle_t q,void *item,TickType_t wait)
{ (void)wait;if(q->count==0U)return pdFALSE;memcpy(item,q->memory+(q->tail%q->capacity)*q->size,q->size);return pdTRUE; }
BaseType_t xQueueReceive(QueueHandle_t q,void *item,TickType_t wait)
{ if(!xQueuePeek(q,item,wait))return pdFALSE;++q->tail;--q->count;return pdTRUE; }
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return q->capacity-q->count; }
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port,uint16_t pin) { return (port->IDR&pin)?GPIO_PIN_SET:GPIO_PIN_RESET; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,GPIO_PinState state)
{ if(state)port->ODR|=pin;else port->ODR&=~pin; }
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer,uint32_t channel)
{ assert(test_primask!=0U);timer->Instance->CCER|=1U<<channel;timer->Instance->CR1|=TIM_CR1_CEN;return HAL_OK; }
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer,uint32_t channel)
{ timer->Instance->CCER&=~(1U<<channel);return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *dma) { dma->State=HAL_DMA_STATE_READY;return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *dma)
{
    ++test_abort_calls;
    if(dma==hw.pyro_dma) assert((GPIOD->MODER&PYRO_MODER_MASK)==0U); /* Drive removed BEFORE abort. */
    if(test_abort_fails)return HAL_ERROR;
    dma->State=HAL_DMA_STATE_READY;dma->Instance->CR&=~DMA_SxCR_EN;return HAL_OK;
}
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef *dma,uint32_t source,uint32_t target,uint32_t count)
{
    assert(source==(uint32_t)pyro_buffer.words && target==(uint32_t)&GPIOD->BSRR && count==2U);
    assert(TIM6->DIER==0U && (TIM6->CR1&TIM_CR1_CEN)==0U);
    ++test_dma_launches;dma->State=HAL_DMA_STATE_BUSY;dma->Instance->CR|=DMA_SxCR_EN;return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *adc) { (void)adc;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc,const ADC_ChannelConfTypeDef *c)
{ assert(c->Rank==ADC_REGULAR_RANK_1);adc->Instance->selected_channel=c->Channel;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc) { adc->Instance->ISR=0U;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc) { (void)adc;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc,uint32_t timeout)
{ assert(timeout==0U);return (adc->Instance->ISR&ADC_FLAG_EOC)?HAL_OK:HAL_TIMEOUT; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc) { return adc->Instance->raw; }
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc,uint32_t *data,uint32_t length)
{ assert(data==adc_buffer.words && length==10U);adc->DMA_Handle->Instance->CR=DMA_SxCR_EN;return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc) { adc->DMA_Handle->Instance->CR=0U;return HAL_OK; }
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc,uint32_t mode,uint32_t diff)
{ (void)adc;(void)mode;(void)diff;return HAL_OK; }
HAL_StatusTypeDef HAL_RAMECC_Init(RAMECC_HandleTypeDef *m) { (void)m;return HAL_OK; }
HAL_StatusTypeDef HAL_RAMECC_StartMonitor(RAMECC_HandleTypeDef *m) { (void)m;return HAL_OK; }
HAL_StatusTypeDef HAL_RAMECC_EnableNotification(RAMECC_HandleTypeDef *m,uint32_t flags) { (void)m;(void)flags;return HAL_OK; }
uint32_t HAL_RAMECC_GetFailingAddress(RAMECC_HandleTypeDef *m) { return m->Instance->FADD; }
void HAL_RAMECC_IRQHandler(RAMECC_HandleTypeDef *m) { HAL_RAMECC_DetectErrorCallback(m); }

/** @brief Reset an inert fixture; startup/linker/hardware calibration are tested separately. */
static void fixture(void)
{
#if ATLAS_SERVO_BENCH
    servo_cancel_epoch=servo_seen_cancel=servo_started_ms=servo_changed_ms=servo_usb_session=0U;
#endif
#if ATLAS_BRINGUP
    bench_gpio_active=false;bench_gpio_started_ms=0U;
    bench_test_usb=(AtlasUsbHealth){.configured=true,.dtr=true};
#endif
    memset(&working,0,sizeof(working));memset(&published,0,sizeof(published));memset(&settings,0,sizeof(settings));
    memset(test_gpio,0,sizeof(test_gpio));memset(test_tim,0,sizeof(test_tim));memset(test_dma,0,sizeof(test_dma));
    memset(test_adc,0,sizeof(test_adc));memset(adcs,0,sizeof(adcs));
    test_tick=1000U;test_permitted=true;emergency_latched=false;hardware_ready=true;started=true;
    configured_locked=false;pulse_active=pulse_complete=pulse_failed=false;test_abort_fails=false;
    owner_cycles_remaining=0U;
#if !ATLAS_SERVO_BENCH
    last_permitted=false;
#endif
    test_primask=test_ipsr=0U;test_safety_pending=false;external_pending=internal_pending=false;
    reference_valid=false;internal_temperature=false;
    reference_vdda=reference_started_ms=internal_started_ms=next_ticket=output_epoch=0U;
    ecc_events=ecc_monitor_register=ecc_failing_word=ecc_error_code=0U;
    for(unsigned i=0;i<3U;++i)timers[i].Instance=&test_tim[i];
    for(unsigned i=0;i<2U;++i){dmas[i].Instance=&test_dma[i];dmas[i].State=HAL_DMA_STATE_READY;adcs[i].Instance=&test_adc[i];adcs[i].DMA_Handle=&dmas[i];}
    hw=(AtlasIoHardware){&adcs[0],&adcs[1],&timers[0],&timers[1],&timers[2],&dmas[1]};
    GPIOD->MODER=PYRO_MODER_OUTPUT;
    working.analog.sampled_at_ms=working.analog.reference_at_ms=test_tick;
    working.analog.valid_mask=0x3FFU;working.analog.millivolts[0]=3300U;working.analog.millivolts[1]=7400U;
    AtlasPyroPolicy_Init(&working.pyro);
    requests=xQueueCreateStatic(ATLAS_IO_QUEUE_CAPACITY,sizeof(IoQueued),request_memory,&requests_control);
    results=xQueueCreateStatic(ATLAS_IO_QUEUE_CAPACITY,sizeof(AtlasIoResult),result_memory,&results_control);
}
/** @brief Exercise copied queue, qualification, bounds and stale-command rejection. */
#if !ATLAS_BRINGUP
static void command_cases(void)
{
    fixture();IoQueued item={0};item.command.type=ATLAS_IO_CONFIGURE;
    assert(io_execute(&item)==ATLAS_ERROR_STATE); /* No zero/default "qualification". */
    item.command.arguments.configuration.electrical_review_complete=true;
    item.command.arguments.configuration.pwm_allowed_mask=1U;
    item.command.arguments.configuration.pwm[0]=(AtlasPwmCalibration){900U,1500U,2100U};
    assert(io_execute(&item)==ATLAS_OK);
    AtlasIoCommand command={.type=ATLAS_IO_PWM_ENABLE,.arguments.channel_mask=1U};
    uint32_t ticket;
    assert(AtlasIo_Submit(&command,&ticket)==ATLAS_OK);
    command.arguments.channel_mask=0U;
    assert(xQueueReceive(requests,&item,0U)==pdTRUE && item.ticket==ticket && item.command.arguments.channel_mask==1U);
    assert(io_execute(&item)==ATLAS_OK && working.pwm_enabled_mask==1U);
    item.command.type=ATLAS_IO_PWM_SET;item.command.arguments.pwm.channel=0U;item.command.arguments.pwm.pulse_us=2101U;
    assert(io_execute(&item)==ATLAS_ERROR_ARGUMENT);
    item.command.arguments.pwm.pulse_us=1500U;item.submitted_ms=test_tick-51U;
    assert(io_execute(&item)==ATLAS_ERROR_STATE);
    item.submitted_ms=test_tick;test_safety_pending=true;
    assert(io_execute(&item)==ATLAS_OK); /* Safety IRQ waits until bounded register work ends. */
    assert(emergency_latched && TIM1->CCER==0U && (GPIOE->MODER&(3U<<18))==(1U<<18));
    assert(io_execute(&item)==ATLAS_ERROR_STATE);
    /* Normal flight API uses the same PCB labels, while retaining its own gates. */
    for(unsigned ch=0U;ch<8U;++ch)
    {
        fixture();working.configured=true;settings.pwm_allowed_mask=255U;
        settings.pwm[ch]=(AtlasPwmCalibration){900U,1500U,2100U};
        const unsigned bank[]={1U,1U,0U,0U,0U,0U,1U,1U};
        const unsigned compare[]={2U,3U,0U,1U,2U,3U,0U,1U};
        item=(IoQueued){.command={.type=ATLAS_IO_PWM_ENABLE,.arguments.channel_mask=(uint8_t)(1U<<ch)},.submitted_ms=test_tick};
        assert(io_execute(&item)==ATLAS_OK);
        assert(test_tim[bank[ch]].CCR[compare[ch]]==1500U);
        item.command=(AtlasIoCommand){.type=ATLAS_IO_PWM_SET,.arguments.pwm={(uint8_t)ch,1600U}};
        assert(io_execute(&item)==ATLAS_OK && test_tim[bank[ch]].CCR[compare[ch]]==1600U);
        item.command=(AtlasIoCommand){.type=ATLAS_IO_PWM_DISABLE,.arguments.channel_mask=(uint8_t)(1U<<ch)};
        assert(io_execute(&item)==ATLAS_OK && TIM1->CCER==0U && TIM3->CCER==0U);
        working.analog.millivolts[1]=8420U;
        item.command=(AtlasIoCommand){.type=ATLAS_IO_PWM_ENABLE,.arguments.channel_mask=(uint8_t)(1U<<ch)};
        item.epoch=output_epoch;
        assert(io_execute(&item)==ATLAS_ERROR_ARGUMENT);
    }
    puts("PASS IO: copied/bounded PWM, all PCB routes, normal voltage gates, expiry and emergency IRQ");
}
/** @brief Verify that OFF commands fence queued assertions and ECC events are consumed once. */
static void inhibition_cases(void)
{
    fixture();test_permitted=false;
    IoQueued low={.command={.type=ATLAS_IO_GPIO_SET,.arguments.gpio={0U,false}}};
    GPIOF->ODR=GPIO_OUT1_Pin;working.gpio_commanded_high=1U;
    assert(io_execute(&low)==ATLAS_OK && (GPIOF->ODR&GPIO_OUT1_Pin)==0U && !configured_locked);
    assert(output_epoch==1U && working.gpio_commanded_high==0U);
    test_permitted=true;working.configured=true;settings.pwm_allowed_mask=1U;
    settings.pwm[0]=(AtlasPwmCalibration){900U,1500U,2100U};
    IoQueued enable={.command={.type=ATLAS_IO_PWM_ENABLE,.arguments.channel_mask=1U},
        .submitted_ms=test_tick,.epoch=output_epoch};
    assert(io_execute(&enable)==ATLAS_OK);
    IoQueued disable={.command={.type=ATLAS_IO_PWM_DISABLE,.arguments.channel_mask=1U}};
    assert(io_execute(&disable)==ATLAS_OK && working.pwm_enabled_mask==0U);
    assert(io_execute(&enable)==ATLAS_ERROR_STATE); /* Accepted before OFF cannot silently re-enable. */
    RAMECC_HandleTypeDef monitor={.Instance=&test_ecc[0],.RAMECCErrorCode=1U};
    monitor.Instance->FADD=123U;HAL_RAMECC_DetectErrorCallback(&monitor);
    assert(ecc_events==1U && ecc_failing_word==123U && ecc_error_code==1U && monitor.RAMECCErrorCode==0U);
    HAL_RAMECC_DetectErrorCallback(&monitor);assert(ecc_events==1U);
    monitor.RAMECCErrorCode=2U;monitor.Instance->FADD=456U;HAL_RAMECC_DetectErrorCallback(&monitor);
    assert(ecc_events==2U && ecc_failing_word==123U && ecc_error_code==1U && emergency_latched);
    puts("PASS IO: unconditional deassertion, OFF generation fences and first-ECC evidence without double counting");
}
/** @brief Check the actual sequencer setup and normal/failing abort paths. */
static void pulse_cases(void)
{
    fixture();assert(io_pyro_start(5U)==ATLAS_ERROR_STATE);
    assert(io_pyro_start(2U)==ATLAS_OK && pulse_active);
    assert(TIM6->PSC==999U && TIM6->ARR==49999U && TIM6->CNT==49999U && TIM6->DIER==TIM_DIER_UDE);
    assert(pyro_buffer.words[0]==PYRO_FIRE3_Pin && pyro_buffer.words[1]==((uint32_t)PYRO_PINS<<16));
    assert(io_pyro_start(0U)==ATLAS_ERROR_STATE);
    /* Model two timer updates while the CPU performs NO pulse servicing. */
    GPIOD->ODR|=pyro_buffer.words[0];test_tick+=500U;GPIOD->ODR&=~(pyro_buffer.words[1]>>16);
    dmas[1].State=HAL_DMA_STATE_READY;DMA1_Stream1->CR=0U;dmas[1].XferCpltCallback(&dmas[1]);
    assert((GPIOD->ODR&PYRO_PINS)==0U && !pulse_active && pulse_complete && TIM6->DIER==0U);
    assert(io_pyro_stop());assert(io_pyro_start(0U)==ATLAS_OK);assert(io_pyro_stop());
    assert(!pulse_active && (GPIOD->MODER&PYRO_MODER_MASK)==PYRO_MODER_OUTPUT);
    assert(io_pyro_start(0U)==ATLAS_OK);test_abort_fails=true;
    assert(!io_pyro_stop() && emergency_latched);
    GPIOD->ODR|=PYRO_FIRE1_Pin; /* A late DMA SET still cannot drive an INPUT pin. */
    assert((GPIOD->MODER&PYRO_MODER_MASK)==0U && io_pyro_start(0U)==ATLAS_ERROR_STATE);
    puts("PASS IO: two-event DMA setup, overlap rejection, completion, abort ordering and late-write inhibition");
}
#endif
#if ATLAS_BRINGUP
/** @brief Diagnostic profile rejects every actuator path, even with forged qualification. */
static void bench_cases(void)
{
    fixture();working.configured=true;test_permitted=true;settings.pyro_enabled=true;
    const AtlasIoCommandType blocked[]={ATLAS_IO_CONFIGURE,ATLAS_IO_PWM_ENABLE,ATLAS_IO_PWM_SET,
        ATLAS_IO_PYRO_ARM,ATLAS_IO_PYRO_REQUEST,ATLAS_IO_GPIO_SET};
    for(unsigned i=0;i<sizeof(blocked)/sizeof(blocked[0]);++i)
    {
        AtlasIoCommand command={.type=blocked[i],.arguments.gpio={0U,true}};
        assert(AtlasIo_Submit(&command,NULL)==ATLAS_ERROR_UNSUPPORTED);
        IoQueued direct={.command=command,.epoch=output_epoch,.submitted_ms=test_tick};
        assert(io_execute(&direct)==ATLAS_ERROR_UNSUPPORTED);
    }
    IoQueued pulse={.command={.type=ATLAS_IO_BENCH_GPIO,.arguments.gpio={0U,true}},
                    .epoch=output_epoch,.submitted_ms=test_tick};
    assert(io_execute(&pulse)==ATLAS_OK && bench_gpio_active && working.gpio_commanded_high==1U);
    assert(io_execute(&pulse)==ATLAS_ERROR_STATE); /* No overlap. */
    test_tick+=999U;working.analog.sampled_at_ms=working.analog.reference_at_ms=test_tick;
    io_bench_gpio_service(test_tick);assert(bench_gpio_active);
    ++test_tick;io_bench_gpio_service(test_tick);
    /* The model records BSRR writes; it does not emulate their silicon ODR side effect. */
    assert(!bench_gpio_active && working.gpio_commanded_high==0U &&
           (GPIOF->BSRR&((uint32_t)GPIO_OUT1_Pin<<16))!=0U);
    assert(io_execute(&pulse)==ATLAS_ERROR_STATE); /* Old age/epoch cannot reassert. */
    for(unsigned reason=0;reason<4U;++reason)
    {
        fixture();pulse.epoch=output_epoch;pulse.submitted_ms=test_tick;
        assert(io_execute(&pulse)==ATLAS_OK);
        if(reason==0U)bench_test_usb.dtr=false;
        if(reason==1U)bench_test_usb.configured=false;
        if(reason==2U)working.analog.millivolts[0]=2800U;
        if(reason==3U)emergency_latched=true;
        io_bench_gpio_service(test_tick);
        assert(!bench_gpio_active && working.gpio_commanded_high==0U);
    }
    fixture();test_tick=UINT32_MAX-500U;pulse.submitted_ms=test_tick;pulse.epoch=output_epoch;
    working.analog.sampled_at_ms=working.analog.reference_at_ms=test_tick;
    assert(io_execute(&pulse)==ATLAS_OK);
    test_tick+=1000U;working.analog.sampled_at_ms=working.analog.reference_at_ms=test_tick;
    io_bench_gpio_service(test_tick);assert(!bench_gpio_active); /* Tick wrap. */
    fixture();pulse.submitted_ms=test_tick;pulse.epoch=output_epoch;
    pulse.command.arguments.gpio.channel=7U;assert(io_execute(&pulse)==ATLAS_ERROR_ARGUMENT);
    pulse.command.arguments.gpio.channel=0U;bench_test_usb.dtr=false;
    assert(io_execute(&pulse)==ATLAS_ERROR_STATE);
    pulse.command.arguments.gpio.high=false;emergency_latched=true;
    assert(io_execute(&pulse)==ATLAS_OK); /* All-low remains possible after loss/fault. */
    assert(test_dma_launches==0U && working.pwm_enabled_mask==0U && !working.pyro.software_armed);
    puts("PASS bring-up IO: PWM/pyro/config denial, bounded GPIO, age/epoch, wrap, disconnect, low rail, emergency");
}
#endif
/** @brief Check completed-only ADC data consumption and single-rank ADC3 sequencing. */
#if ATLAS_SERVO_BENCH
static IoQueued servo_request(AtlasIoCommand command)
{
    IoQueued queued;
    assert(AtlasIo_Submit(&command,NULL)==ATLAS_OK);
    assert(xQueueReceive(requests,&queued,0U)==pdTRUE);
    return queued;
}
static void servo_fresh(void)
{ working.analog.sampled_at_ms=working.analog.reference_at_ms=test_tick; }
/* Exercise the actual task, including policy checks after the ServoBench service.
 * The second iteration is where the old shared 4.8-8.4 V gate cut live PWM. */
static void servo_owner_loop_cases(void)
{
    fixture();
    for(unsigned i=0U;i<2U;++i)
    {
        io_servo_adc_average(&adcs[i]);
        assert(adcs[i].Init.OversamplingMode==ENABLE && adcs[i].Init.Oversampling.Ratio==16U);
        assert(adcs[i].Init.Oversampling.RightBitShift==ADC_RIGHTBITSHIFT_4);
        assert(adcs[i].Init.Oversampling.TriggeredMode==ADC_TRIGGEREDMODE_SINGLE_TRIGGER);
        assert(adcs[i].Init.Oversampling.OversamplingStopReset==ADC_REGOVERSAMPLING_CONTINUED_MODE);
    }
    const uint32_t accepted_mv[] = {1U,4799U,8420U,8550U};
    for (unsigned i = 0U; i < sizeof(accepted_mv)/sizeof(accepted_mv[0]); ++i)
    {
        fixture();test_permitted=false;
        working.analog.millivolts[1]=accepted_mv[i];
        working.analog.millivolts[0]=4000U;working.analog.millivolts[4]=12000U;
        working.analog.valid_mask=2U;
        AtlasIoCommand enable={.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}};
        assert(AtlasIo_Submit(&enable,NULL)==ATLAS_OK);
        owner_cycles_remaining=2U;
        if (setjmp(owner_loop_exit)==0) io_task(NULL);
        assert(published.heartbeat==2U && published.status==ATLAS_OK);
        assert(published.pwm_enabled_mask==1U && published.commanded_pwm_us[0]==1520U);
        assert(published.servo_ready && published.servo_stop_reason==0U);
        assert(!published.pyro.software_armed && test_dma_launches==0U);
    }
    fixture();test_permitted=false;
    AtlasIoCommand enable={.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}};
    assert(AtlasIo_Submit(&enable,NULL)==ATLAS_OK);
    owner_cycles_remaining=2U;if(setjmp(owner_loop_exit)==0)io_task(NULL);
    working.analog.millivolts[1]=8551U;
    owner_cycles_remaining=1U;if(setjmp(owner_loop_exit)==0)io_task(NULL);
    assert(published.pwm_enabled_mask==0U && published.servo_stop_reason==4U);
    assert(published.servo_stop_pwm_mv==8551U && published.servo_target_us==0U);
    assert(TIM1->CCER==0U && TIM3->CCER==0U);
    puts("PASS ServoBench full owner loop: accepted voltage survives; 8551 mV cuts and records evidence");
}
/* Expected hardware destinations from manufacturing copper and placement,
 * independently stated here rather than using the implementation routing table. */
static void servo_cases(void)
{
    TIM_TypeDef *const expected_timer[]={TIM3,TIM3,TIM1,TIM1,TIM1,TIM1,TIM3,TIM3};
    GPIO_TypeDef *const expected_port[]={GPIOB,GPIOB,GPIOE,GPIOE,GPIOE,GPIOE,GPIOC,GPIOC};
    const unsigned expected_compare[]={2U,3U,0U,1U,2U,3U,0U,1U};
    const unsigned expected_shift[]={0U,2U,18U,22U,26U,28U,12U,14U};
    for (unsigned ch=0U;ch<8U;++ch)
    {
        fixture();test_permitted=false; /* Flight output permission stays false. */
        AtlasIoCommand enable={.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={(uint8_t)ch,1320U,1720U}};
        IoQueued queued=servo_request(enable);
        assert(io_execute(&queued)==ATLAS_OK && working.pwm_enabled_mask==(1U<<ch));
        assert(working.commanded_pwm_us[ch]==1520U);
        assert(expected_timer[ch]->CCER==(1U<<(expected_compare[ch]*4U)));
        assert(expected_timer[ch]->CCR[expected_compare[ch]]==1520U);
        assert((expected_port[ch]->MODER&(3U<<expected_shift[ch]))==(2U<<expected_shift[ch]));
        for(unsigned bank=0;bank<2U;++bank)
            for(unsigned compare=0;compare<4U;++compare)
                if(&test_tim[bank]!=expected_timer[ch]||compare!=expected_compare[ch])
                    assert(test_tim[bank].CCR[compare]==0U);
        assert(io_execute(&queued)==ATLAS_ERROR_BUSY); /* Never overlap or silently recenter. */
        IoQueued move=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_SET,.arguments.pwm={(uint8_t)ch,1721U}});
        assert(io_execute(&move)==ATLAS_ERROR_ARGUMENT);
        move.command.arguments.pwm.pulse_us=1720U;
        expected_timer[ch]->CNT=1234U;expected_timer[ch]->EGR=0U;
        const uint32_t running_ccer=expected_timer[ch]->CCER;
        const uint32_t running_cr1=expected_timer[ch]->CR1;
        assert(io_execute(&move)==ATLAS_OK && working.commanded_pwm_us[ch]==1720U);
        assert(working.servo_target_us==1720U);
        assert(expected_timer[ch]->CCR[expected_compare[ch]]==1720U);
        /* SET writes one preloaded destination without restarting the timer or
         * issuing an update event that could truncate the current PWM pulse. */
        assert(expected_timer[ch]->CNT==1234U && expected_timer[ch]->EGR==0U);
        assert(expected_timer[ch]->CCER==running_ccer && expected_timer[ch]->CR1==running_cr1);
        for(unsigned step=1;step<=20U;++step)
        {
            test_tick+=5U;servo_fresh();io_bench_servo_service(test_tick);
            assert(working.commanded_pwm_us[ch]==1720U);
            assert(expected_timer[ch]->CCR[expected_compare[ch]]==1720U);
        }
        /* Reversing uses the same direct-position path as an outward move. */
        move=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_SET,.arguments.pwm={(uint8_t)ch,1320U}});
        assert(io_execute(&move)==ATLAS_OK && working.commanded_pwm_us[ch]==1320U);
        assert(expected_timer[ch]->CCR[expected_compare[ch]]==1320U && working.servo_target_us==1320U);
        assert(expected_timer[ch]->CNT==1234U && expected_timer[ch]->EGR==0U);
        test_tick+=100U;servo_fresh();io_bench_servo_service(test_tick);
        assert(working.commanded_pwm_us[ch]==1320U);
        move=servo_request(move.command);
        AtlasIo_BenchServoStop(); /* Register deassertion even before the owner resumes. */
        assert(TIM1->CCER==0U && TIM3->CCER==0U);
        assert(io_execute(&move)==ATLAS_ERROR_STATE); /* Stop fences accepted-before-stop work. */
        io_bench_servo_service(test_tick);
        assert(working.pwm_enabled_mask==0U && working.servo_stop_reason==1U);
        assert(working.servo_target_us==0U);
        assert((expected_port[ch]->MODER&(3U<<expected_shift[ch]))==(1U<<expected_shift[ch]));
        const unsigned last_compare=expected_timer[ch]->CCR[expected_compare[ch]];
        test_tick+=5U;servo_fresh();io_bench_servo_service(test_tick);
        assert(expected_timer[ch]->CCR[expected_compare[ch]]==last_compare && TIM1->CCER==0U && TIM3->CCER==0U);
        assert(!working.pyro.software_armed && test_dma_launches==0U);
        /* KST's factory nominal -50/0/+50 degrees, on each physical route. */
        enable.arguments.servo.minimum_us=1000U;enable.arguments.servo.maximum_us=2000U;
        queued=servo_request(enable);assert(io_execute(&queued)==ATLAS_OK);
        const uint16_t full_positions[]={1000U,1500U,2000U,1500U};
        for(unsigned position=0;position<4U;++position)
        {
            move=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_SET,
                .arguments.pwm={(uint8_t)ch,full_positions[position]}});
            assert(io_execute(&move)==ATLAS_OK);
            assert(working.commanded_pwm_us[ch]==full_positions[position]);
            assert(expected_timer[ch]->CCR[expected_compare[ch]]==full_positions[position]);
        }
        move.command.arguments.pwm.pulse_us=999U;assert(io_execute(&move)==ATLAS_ERROR_ARGUMENT);
        move.command.arguments.pwm.pulse_us=2001U;assert(io_execute(&move)==ATLAS_ERROR_ARGUMENT);
        assert(expected_timer[ch]->CCR[expected_compare[ch]]==1500U);
        AtlasIo_BenchServoStop();io_bench_servo_service(test_tick);
    }
    for (unsigned reason=0U;reason<10U;++reason)
    {
        fixture();IoQueued enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}});
        assert(io_execute(&enable)==ATLAS_OK);
        if(reason==0U)bench_test_usb.dtr=false;
        if(reason==1U)bench_test_usb.configured=false;
        if(reason==2U)++bench_test_usb.session;
        if(reason==3U)working.analog.millivolts[1]=8551U;
        if(reason==4U)working.analog.millivolts[1]=0U;
        if(reason==5U)working.status=ATLAS_ERROR_IO;
        if(reason==6U)working.analog.reference_at_ms=test_tick-251U;
        if(reason==7U)working.analog.valid_mask&=~2U;
        if(reason==8U)working.analog.sampled_at_ms=test_tick-101U;
        if(reason==9U)AtlasIo_EmergencyStop();
        io_bench_servo_service(test_tick);
        assert(working.pwm_enabled_mask==0U);
        assert(io_execute(&enable)==ATLAS_ERROR_STATE);
    }
    const uint32_t accepted_mv[]={1U,4799U,8301U,8420U,8550U};
    for (unsigned i=0U;i<sizeof(accepted_mv)/sizeof(accepted_mv[0]);++i)
    {
        fixture();working.analog.millivolts[1]=accepted_mv[i];
        working.analog.millivolts[0]=4000U;working.analog.millivolts[4]=12000U;
        working.analog.valid_mask=2U; /* Unrelated ranks no longer gate ServoBench. */
        IoQueued enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}});
        assert(io_execute(&enable)==ATLAS_OK);
        io_bench_servo_service(test_tick);
        assert(working.pwm_enabled_mask==1U && working.servo_ready);
        assert(!working.pyro.software_armed && test_dma_launches==0U);
    }
    fixture();test_tick=UINT32_MAX-1000U;servo_fresh();
    IoQueued enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={7U,900U,2100U}});
    assert(io_execute(&enable)==ATLAS_OK);
    test_tick+=2999U;servo_fresh();io_bench_servo_service(test_tick);
    assert(working.pwm_enabled_mask==128U && working.servo_remaining_ms==1U);
    ++test_tick;servo_fresh();io_bench_servo_service(test_tick);
    assert(working.pwm_enabled_mask==0U && working.servo_stop_reason==2U);
    fixture();enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}});
    assert(io_execute(&enable)==ATLAS_OK);
    for(unsigned step=1U;step<=15U;++step)
    {
        test_tick+=2000U;servo_fresh();
        IoQueued move=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_SET,.arguments.pwm={0U,1500U}});
        assert(io_execute(&move)==(step<15U?ATLAS_OK:ATLAS_ERROR_STATE));
        io_bench_servo_service(test_tick);
    }
    assert(working.pwm_enabled_mask==0U); /* Motion cannot extend the 30 s session. */
    fixture();enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}});
    AtlasIo_BenchServoStop();
    assert(io_execute(&enable)==ATLAS_ERROR_STATE); /* Stop before queued enable executes. */
    fixture();enable=servo_request((AtlasIoCommand){.type=ATLAS_IO_BENCH_SERVO_ENABLE,.arguments.servo={0U,1320U,1720U}});
    ++bench_test_usb.session;
    assert(io_execute(&enable)==ATLAS_ERROR_STATE); /* Rapid USB reconnect cannot replay it. */
    puts("PASS ServoBench: eight routes, direct targets/full range, timer phase retained, stop fence, USB epochs, 8550/8551mV boundary, ADC/fault cutoff, idle/wrap/30s expiry");
}
#endif
static void analog_cases(void)
{
    fixture();reference_valid=true;reference_vdda=3300U;reference_started_ms=test_tick;
    for(unsigned i=0;i<10U;++i)adc_buffer.samples[i]=32768U;
    external_pending=true;external_started_ms=test_tick;adc_complete=false;adc_failed=false;
    assert(!io_sample(test_tick));
    HAL_ADC_ConvCpltCallback(&adcs[0]);adc_pulse_epoch=pulse_epoch;
    assert(io_sample(test_tick) && working.analog.sequence==1U && working.analog.millivolts[0]==3300U);
    assert(io_internal_start(false)==HAL_OK && ADC3->selected_channel==ADC_CHANNEL_VREFINT);
    ADC3->raw=test_vref_cal;ADC3->ISR=ADC_FLAG_EOC;io_reference(test_tick);
    assert(reference_vdda==3300U && reference_valid && working.reference_computed_mv==3300U);
    assert(working.reference_vref_raw==test_vref_cal && working.status==ATLAS_OK);
    assert(ADC3->selected_channel==ADC_CHANNEL_TEMPSENSOR && internal_pending);
    ADC3->raw=test_temp_cal1;ADC3->ISR=ADC_FLAG_EOC;io_reference(test_tick);
    assert(!internal_pending && working.analog.die_temperature_c==30);
    assert(working.reference_failure_stage==ATLAS_IO_REFERENCE_FAILURE_NONE &&
           working.reference_temperature_channel && working.reference_raw==test_temp_cal1);
    external_started_ms=test_tick-21U;assert(!io_sample(test_tick));
    assert(emergency_latched && working.status!=ATLAS_OK);

    /* The released v1.0.1 log could only say IO. Prove that this image retains
     * the exact ADC3 phase and raw evidence without changing conversion policy. */
    fixture();assert(io_internal_start(false)==HAL_OK);
    ADC3->raw=1U;ADC3->ISR=ADC_FLAG_EOC;io_reference(test_tick);
    assert(working.status==ATLAS_ERROR_IO && emergency_latched);
    assert(working.reference_failure_stage==ATLAS_IO_REFERENCE_FAILURE_RAW_RANGE &&
           !working.reference_temperature_channel && working.reference_raw==1U &&
           working.reference_hal_status==HAL_OK && working.reference_hal_error==0U);
    io_reference_note_failure(ATLAS_IO_REFERENCE_FAILURE_TIMEOUT,true,HAL_TIMEOUT,99U);
    assert(working.reference_failure_stage==ATLAS_IO_REFERENCE_FAILURE_RAW_RANGE &&
           !working.reference_temperature_channel && working.reference_raw==1U);
    puts("PASS IO: complete-scan gating, ADC3 VREF/temperature sequencing, timeout and first-failure evidence");
}
/** @brief Execute the adapter boundary cases. @return Zero after successful assertions. */
int main(void)
{
#if ATLAS_BRINGUP
    bench_cases();
#if ATLAS_SERVO_BENCH
    servo_owner_loop_cases();
    servo_cases();
#endif
#else
    command_cases();inhibition_cases();pulse_cases();
#endif
    analog_cases();return 0;
}
