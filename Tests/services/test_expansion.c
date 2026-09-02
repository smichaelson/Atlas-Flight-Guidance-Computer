/** @file test_expansion.c @brief Test real expansion owner and SPI CS boundaries.
 * Major functions: main checks copied requests, I2C addressing, SPI timeout/CS,
 * UART baud budgets and queue backpressure. UART transport itself is covered by
 * the host protocol suite; external device protocols and electrical IO are not modeled. */
#include "service_model.h"
#include "../../App/Src/atlas_expansion.c"
#include <assert.h>
#include <stdio.h>
static UART_HandleTypeDef uart={.Instance=UART4,.Init={115200U}};
static I2C_HandleTypeDef i2c={I2C2};
static SPI_HandleTypeDef spi={SPI3};
static HAL_StatusTypeDef hal_result;
static unsigned transactions,uart_writes,upkeep;
static uint32_t i2c_error;
static uint16_t last_address,last_reg,last_reg_size;
static uint8_t copied[64],incoming[64];
static size_t incoming_size;

AtlasStatus AtlasUartTransport_Init(AtlasUartTransport *transport,UART_HandleTypeDef *port)
{ memset(transport,0,sizeof(*transport));transport->uart=port;return ATLAS_OK; }
AtlasStatus AtlasUartTransport_Start(AtlasUartTransport *transport) { transport->running=true;return ATLAS_OK; }
AtlasStatus AtlasUartTransport_Service(AtlasUartTransport *transport) { assert(transport->running);++upkeep;return ATLAS_OK; }
size_t AtlasUartTransport_Read(AtlasUartTransport *transport,uint8_t *data,size_t capacity)
{ (void)transport;assert(incoming_size<=capacity);size_t count=incoming_size;memcpy(data,incoming,count);incoming_size=0U;return count; }
AtlasStatus AtlasUartTransport_Write(AtlasUartTransport *transport,const uint8_t *data,size_t length,uint32_t timeout)
{ assert(transport->uart==&uart && timeout==5U && length<=32U);memcpy(copied,data,length);++uart_writes;return ATLAS_OK; }
AtlasStatus AtlasUartTransport_ReconfigureBaud(AtlasUartTransport *transport,uint32_t baud)
{ transport->uart->Init.BaudRate=baud;return ATLAS_OK; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *port,uint8_t *tx,uint8_t *rx,uint16_t size,uint32_t timeout)
{
    assert(port==&spi && timeout==5U && (GPIOG->ODR&CS_SPI_EXT_Pin)==0U);
    assert((GPIOG->IDR&CS_LSM6DSV16B_Pin)!=0U);++transactions;
    for(unsigned i=0;i<size;++i)rx[i]=tx[i]^0xFFU;
    return hal_result;
}
/** @brief Record one inert I2C operation. @param port I2C2. @param address Shifted address.
 * @param data IO bytes. @param size Count. @param timeout Required bound. @return Injected HAL result. */
static HAL_StatusTypeDef i2c_call(I2C_HandleTypeDef *port,uint16_t address,uint8_t *data,uint16_t size,uint32_t timeout)
{ assert(port==&i2c && timeout==5U && size<=32U);last_address=address;++transactions;memcpy(copied,data,size);return hal_result; }
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *p,uint16_t a,uint8_t *d,uint16_t n,uint32_t t)
{ return i2c_call(p,a,d,n,t); }
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *p,uint16_t a,uint8_t *d,uint16_t n,uint32_t t)
{ memset(d,0x6BU,n);return i2c_call(p,a,d,n,t); }
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *p,uint16_t a,uint16_t reg,uint16_t rs,uint8_t *d,uint16_t n,uint32_t t)
{ last_reg=reg;last_reg_size=rs;return i2c_call(p,a,d,n,t); }
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *p,uint16_t a,uint16_t reg,uint16_t rs,uint8_t *d,uint16_t n,uint32_t t)
{ last_reg=reg;last_reg_size=rs;memset(d,0x7CU,n);return i2c_call(p,a,d,n,t); }
uint32_t HAL_I2C_GetError(I2C_HandleTypeDef *port) { assert(port==&i2c);return i2c_error; }
/** @brief Submit/execute/read a single request. @param request Copied request.
 * @return Retained result from the production result queue. */
static AtlasExpansionResult execute(const AtlasExpansionRequest *request)
{
    uint32_t ticket;AtlasExpansionResult result;
    assert(AtlasExpansion_Submit(request,&ticket)==ATLAS_OK);
    assert(AtlasExpansion_Service(true));assert(AtlasExpansion_Receive(&result));
    assert(result.ticket==ticket && result.operation==request->operation);return result;
}
/** @brief Run boundary and backpressure assertions. @return Zero when all pass. */
int main(void)
{
    TestRuntimeReset();hal_result=HAL_OK;
    assert(AtlasExpansion_Start(&uart,&i2c,&spi)==ATLAS_OK && (GPIOG->ODR&CS_SPI_EXT_Pin)!=0U);
    test_scheduler=taskSCHEDULER_RUNNING;GPIOG->IDR=CS_LSM6DSV16B_Pin;
    AtlasExpansionRequest r={.operation=ATLAS_EXP_I2C_WRITE,.length=2U,.address_7bit=0x42U,.data={1U,2U}};
    uint32_t ticket;assert(AtlasExpansion_Submit(&r,&ticket)==ATLAS_OK);r.data[0]=0xEEU;
    assert(!AtlasExpansion_Service(false) && transactions==0U && upkeep==1U);
    assert(AtlasExpansion_Service(true));AtlasExpansionResult result;
    assert(AtlasExpansion_Receive(&result) && result.ticket==ticket && result.status==ATLAS_OK);
    assert(last_address==0x84U && copied[0]==1U);
    r.address_7bit=7U;assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_ERROR_ARGUMENT);
    r.address_7bit=0x78U;assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_ERROR_ARGUMENT);
    r.address_7bit=0x42U;r.length=33U;assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_ERROR_ARGUMENT);r.length=2U;
    r.operation=ATLAS_EXP_I2C_REGISTER_READ;r.register_address=0x1234U;
    assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_ERROR_ARGUMENT);r.register_16bit=true;
    result=execute(&r);assert(result.status==ATLAS_OK && result.length==2U && result.data[0]==0x7CU);
    assert(last_reg==0x1234U && last_reg_size==I2C_MEMADD_SIZE_16BIT);
    hal_result=HAL_ERROR;i2c_error=HAL_I2C_ERROR_AF;result=execute(&r);
    assert(result.status==ATLAS_ERROR_NACK && result.length==0U && result.data[0]==0U);
    hal_result=HAL_TIMEOUT;result=execute(&r);assert(result.status==ATLAS_ERROR_TIMEOUT && result.length==0U);

    r.operation=ATLAS_EXP_SPI_EXCHANGE;result=execute(&r);
    assert(result.status==ATLAS_ERROR_TIMEOUT && (GPIOG->ODR&CS_SPI_EXT_Pin)!=0U);
    hal_result=HAL_OK;result=execute(&r);assert(result.status==ATLAS_OK && result.data[0]==(uint8_t)(r.data[0]^0xFFU));
    unsigned previous=transactions;GPIOG->IDR=0U;result=execute(&r);
    assert(result.status==ATLAS_ERROR_BUSY && transactions==previous && (GPIOG->ODR&CS_SPI_EXT_Pin)!=0U);
    GPIOG->IDR=CS_LSM6DSV16B_Pin;
    r.operation=ATLAS_EXP_UART_HOST_BAUD;r.baud_rate=9600U;assert(execute(&r).status==ATLAS_OK);
    r.operation=ATLAS_EXP_UART_WRITE;r.length=4U;assert(execute(&r).status==ATLAS_ERROR_ARGUMENT && uart_writes==0U);
    r.length=3U;assert(execute(&r).status==ATLAS_OK && uart_writes==1U);

    for(unsigned i=0;i<4U;++i){assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_OK);assert(AtlasExpansion_Service(true));}
    assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_OK);previous=uart_writes;
    memcpy(incoming,"abc",3U);incoming_size=3U;
    assert(!AtlasExpansion_Service(true) && uart_writes==previous); /* Full result queue; RX still drained. */
    uint8_t out[64];assert(AtlasExpansion_ReadUart(out,sizeof(out))==3U && memcmp(out,"abc",3U)==0);
    assert(AtlasExpansion_Receive(&result) && AtlasExpansion_Service(true) && uart_writes==previous+1U);
    test_ipsr=1U;assert(AtlasExpansion_Submit(&r,NULL)==ATLAS_ERROR_STATE && !AtlasExpansion_Service(true));
    puts("Expansion: copied requests, I2C addresses/NACK, SPI CS cleanup, UART budget and backpressure PASS");
    return 0;
}
