/**
 * @file atlas_status.c
 * @brief Diagnostic names for shared Atlas firmware status codes.
 *
 * Major functions:
 * - AtlasStatus_Name(): converts a typed result to stable diagnostic text.
 */

#include "atlas_status.h"

/**
 * @brief Return a stable, human-readable name for a status value.
 * @param status Status value to translate.
 * @return Constant string; never NULL.
 */
const char *AtlasStatus_Name(AtlasStatus status)
{
    switch (status)
    {
        case ATLAS_OK:                return "OK";
        case ATLAS_ERROR_NULL:        return "NULL";
        case ATLAS_ERROR_ARGUMENT:    return "ARGUMENT";
        case ATLAS_ERROR_BUSY:        return "BUSY";
        case ATLAS_ERROR_NOT_READY:   return "NOT_READY";
        case ATLAS_ERROR_TIMEOUT:     return "TIMEOUT";
        case ATLAS_ERROR_IO:          return "IO";
        case ATLAS_ERROR_IDENTITY:    return "IDENTITY";
        case ATLAS_ERROR_CRC:         return "CRC";
        case ATLAS_ERROR_PROTOCOL:    return "PROTOCOL";
        case ATLAS_ERROR_NACK:        return "NACK";
        case ATLAS_ERROR_OVERFLOW:    return "OVERFLOW";
        case ATLAS_ERROR_UNSUPPORTED: return "UNSUPPORTED";
        case ATLAS_ERROR_STATE:       return "STATE";
        default:                      return "UNKNOWN";
    }
}
