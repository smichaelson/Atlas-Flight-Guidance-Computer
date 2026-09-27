/**
 * @file atlas_telemetry_boot.c
 * @brief Bounded STM32H743 RNG boot identifier, isolated from the sensor/USB clocks.
 * Major functions: NewBoot enables HSI48 and RNG briefly before RTOS scheduling;
 * checks clock/seed errors before and after each read, then restores clock selection.
 * Reference: ST RM0433, RNG initialization and RNG_SR/RNG_DR. No random keys are created.
 */
#include "atlas_telemetry.h"
#include "main.h"

/** @brief Obtain two nonzero, distinct RNG words with a total 20 ms bound.
 * @param boot Identifier; both words zero if the hardware fails any check. */
void AtlasTelemetry_NewBoot(uint32_t boot[2])
{
    boot[0]=boot[1]=0U;
    const uint32_t began=HAL_GetTick();
    const bool had_hsi48=(RCC->CR&RCC_CR_HSI48ON)!=0U;
    const uint32_t selection=RCC->D2CCIP2R&RCC_D2CCIP2R_RNGSEL;
    RCC->CR|=RCC_CR_HSI48ON;
    while(!(RCC->CR&RCC_CR_HSI48RDY)&&(uint32_t)(HAL_GetTick()-began)<20U){}
    if(RCC->CR&RCC_CR_HSI48RDY){
        __HAL_RCC_RNG_CLK_ENABLE();
        __HAL_RCC_RNG_FORCE_RESET();__HAL_RCC_RNG_RELEASE_RESET();
        __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
        RNG->CR=RNG_CR_RNGEN; /* CED=0 leaves clock-error detection enabled. */
        const uint32_t errors=RNG_SR_CECS|RNG_SR_SECS|RNG_SR_CEIS|RNG_SR_SEIS;
        bool ok=true;
        for(unsigned i=0;i<2U&&ok;++i){
            while(!(RNG->SR&(RNG_SR_DRDY|errors))&&(uint32_t)(HAL_GetTick()-began)<20U){}
            if(!(RNG->SR&RNG_SR_DRDY)||(RNG->SR&errors)){ok=false;break;}
            boot[i]=RNG->DR;
            if((RNG->SR&errors)||!boot[i]||(i&&boot[0]==boot[1]))ok=false;
        }
        RNG->CR=0U;__HAL_RCC_RNG_CLK_DISABLE();
        MODIFY_REG(RCC->D2CCIP2R,RCC_D2CCIP2R_RNGSEL,selection);
        if(!ok)boot[0]=boot[1]=0U;
    }
    if(!had_hsi48)RCC->CR&=~RCC_CR_HSI48ON;
}
