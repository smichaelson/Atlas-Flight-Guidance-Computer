/**
 * @file atlas_spi_device.h
 * @brief Bounded STM32 HAL SPI transactions with explicit software chip select.
 *
 * Major functions:
 * - AtlasSpiDevice_Init(): binds a HAL SPI handle and an active-low chip select.
 * - AtlasSpiDevice_Transfer(): performs one complete, timeout-bounded transaction.
 */

#ifndef ATLAS_SPI_DEVICE_H
#define ATLAS_SPI_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "main.h"
#include "atlas_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Description of one active-low SPI peripheral on an Atlas bus. */
typedef struct
{
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;
    uint32_t timeout_ms;
} AtlasSpiDevice;

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
                                uint32_t timeout_ms);

/**
 * @brief Perform one full-duplex SPI transaction while chip select is asserted.
 * @param device Initialized SPI device object.
 * @param tx Transmit bytes; must contain length bytes.
 * @param rx Receive destination; must contain length bytes.
 * @param length Number of bytes to exchange; range 1..65535.
 * @return ATLAS_OK, ATLAS_ERROR_BUSY, ATLAS_ERROR_TIMEOUT, or ATLAS_ERROR_IO.
 * @note Shared-bus serialization is the caller's responsibility.
 */
AtlasStatus AtlasSpiDevice_Transfer(AtlasSpiDevice *device,
                                    const uint8_t *tx,
                                    uint8_t *rx,
                                    size_t length);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_SPI_DEVICE_H */
