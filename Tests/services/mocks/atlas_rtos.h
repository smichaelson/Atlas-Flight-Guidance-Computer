/** @file atlas_rtos.h @brief Isolated output-gate boundary for service testing.
 * Major functions: query permission and permanently inhibit the mocked output owner. */
#ifndef ATLAS_SERVICE_RTOS_H
#define ATLAS_SERVICE_RTOS_H
#include <stdbool.h>
bool AtlasRtos_OutputsPermitted(void);
void AtlasRtos_InhibitOutputs(void);
#endif
