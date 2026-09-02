/**
 * @file atlas_status.h
 * @brief Shared, typed status codes for Atlas project-owned firmware.
 *
 * Major functions and definitions:
 * - AtlasStatus: common success, timeout, transport, identity, and protocol results.
 * - AtlasStatus_Name(): stable diagnostic text for logs and bring-up reports.
 */

#ifndef ATLAS_STATUS_H
#define ATLAS_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Result codes returned by Atlas drivers and services. */
typedef enum
{
    ATLAS_OK = 0,
    ATLAS_ERROR_NULL,
    ATLAS_ERROR_ARGUMENT,
    ATLAS_ERROR_BUSY,
    ATLAS_ERROR_NOT_READY,
    ATLAS_ERROR_TIMEOUT,
    ATLAS_ERROR_IO,
    ATLAS_ERROR_IDENTITY,
    ATLAS_ERROR_CRC,
    ATLAS_ERROR_PROTOCOL,
    ATLAS_ERROR_NACK,
    ATLAS_ERROR_OVERFLOW,
    ATLAS_ERROR_UNSUPPORTED,
    ATLAS_ERROR_STATE
} AtlasStatus;

/**
 * @brief Return a stable, human-readable name for a status value.
 * @param status Status value to translate.
 * @return Constant string; never NULL.
 */
const char *AtlasStatus_Name(AtlasStatus status);

#ifdef __cplusplus
}
#endif

#endif /* ATLAS_STATUS_H */
