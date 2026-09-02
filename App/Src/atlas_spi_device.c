/**
 * @file atlas_spi_device.c
 * @brief Bounded STM32 HAL SPI transactions with fail-safe chip-select release.
 *
 * Major functions:
 * - AtlasSpiDevice_Init(): validates and binds a software-controlled SPI device.
 * - AtlasSpiDevice_Transfer(): exchanges a complete frame and always releases CS.
 */

#include "atlas_spi_device.h"

/**
 * @brief Translate the HAL result used by a blocking SPI transaction.
 * @param hal_status Result returned by STM32 HAL.
 * @return Corresponding Atlas status code.
 */
static AtlasStatus atlas_spi_from_hal(HAL_StatusTypeDef hal_status)
{
    if (hal_status == HAL_OK)
    {
        return ATLAS_OK;
    }
    if (hal_status == HAL_BUSY)
    {
        return ATLAS_ERROR_BUSY;
    }
    if (hal_status == HAL_TIMEOUT)
    {
        return ATLAS_ERROR_TIMEOUT;
    }
    return ATLAS_ERROR_IO;
}

/**
 * @brief Bind a SPI device and force its chip select inactive.
 * @param device Destination device object.
 * @param spi Initialized STM32 HAL SPI handle.
 * @param cs_port GPIO port containing the device chip-select pin.
 * @param cs_pin GPIO pin mask for the active-low chip select.
 * @param timeout_ms Nonzero HAL transaction timeout in milliseconds.
 * @return ATLAS_OK or a parameter error.
 */
AtlasStatus AtlasSpiDevice_Init(AtlasSpiDevice *device,
                                SPI_HandleTypeDef *spi,
                                GPIO_TypeDef *cs_port,
                                uint16_t cs_pin,
                                uint32_t timeout_ms)
{
    if ((device == NULL) || (spi == NULL) || (cs_port == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((cs_pin == 0U) || (timeout_ms == 0U))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    device->spi = spi;
    device->cs_port = cs_port;
    device->cs_pin = cs_pin;
    device->timeout_ms = timeout_ms;

    /* The shared bus is safe only when every unrelated slave remains deselected. */
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
    return ATLAS_OK;
}

/**
 * @brief Perform one full-duplex SPI transaction while chip select is asserted.
 * @param device Initialized SPI device object.
 * @param tx Transmit bytes; must contain length bytes.
 * @param rx Receive destination; must contain length bytes.
 * @param length Number of bytes to exchange; range 1..65535.
 * @return ATLAS_OK, ATLAS_ERROR_BUSY, ATLAS_ERROR_TIMEOUT, or ATLAS_ERROR_IO.
 */
AtlasStatus AtlasSpiDevice_Transfer(AtlasSpiDevice *device,
                                    const uint8_t *tx,
                                    uint8_t *rx,
                                    size_t length)
{
    HAL_StatusTypeDef hal_status;

    if ((device == NULL) || (tx == NULL) || (rx == NULL))
    {
        return ATLAS_ERROR_NULL;
    }
    if ((device->spi == NULL) || (device->cs_port == NULL))
    {
        return ATLAS_ERROR_STATE;
    }
    if ((length == 0U) || (length > UINT16_MAX))
    {
        return ATLAS_ERROR_ARGUMENT;
    }

    HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_RESET);
    hal_status = HAL_SPI_TransmitReceive(device->spi,
                                         (uint8_t *)(uintptr_t)tx,
                                         rx,
                                         (uint16_t)length,
                                         device->timeout_ms);
    /* Release CS on every HAL outcome so one failed slave cannot hold the bus selected. */
    HAL_GPIO_WritePin(device->cs_port, device->cs_pin, GPIO_PIN_SET);

    return atlas_spi_from_hal(hal_status);
}
