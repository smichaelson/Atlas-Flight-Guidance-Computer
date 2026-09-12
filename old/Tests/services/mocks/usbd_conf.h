/** @file usbd_conf.h @brief Host boundary for the real ST USB device core/class.
 * Major definitions: production CDC limits, static allocation and modeled PCD endpoints. */
#ifndef ATLAS_USB_TEST_CONF_H
#define ATLAS_USB_TEST_CONF_H
#include "main.h"
#include <string.h>
#define USBD_MAX_NUM_INTERFACES 2U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_STR_DESC_SIZ 512U
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_SELF_POWERED 1U
#define DEVICE_FS 0U
#define __PACKED __attribute__((packed))
#define __STATIC_INLINE static inline
#define __IO volatile
#define UNUSED(value) ((void)(value))
typedef struct { uint32_t maxpacket; } TestUsbEndpoint;
typedef struct { TestUsbEndpoint IN_ep[16], OUT_ep[16]; } PCD_HandleTypeDef;
void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *data);
void TestUsbDelay(uint32_t ms);
#define USBD_malloc USBD_static_malloc
#define USBD_free USBD_static_free
#define USBD_memcpy memcpy
#define USBD_memset memset
#define USBD_Delay TestUsbDelay
#define USBD_UsrLog(...)
#define USBD_ErrLog(...)
#define USBD_DbgLog(...)
#endif
