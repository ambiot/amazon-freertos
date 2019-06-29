 /******************************************************************************
  *
  * Name: sys-support.h - System type support for Linux
  *       $Revision: 1.1.1.1 $
  *
  *****************************************************************************/

#ifndef __OS_SUPPORT_H__
#define __OS_SUPPORT_H__


#include <basic_types.h>
#include "osdep_service.h"

#define RTL_HZ                          100

#if defined(CONFIG_MBED_ENALBED)
#define RtlKmalloc(size, flag)          malloc(size)
#define RtlKfree(pv)                    free(pv)
#else
#define RtlKmalloc(size, flag)          pvPortMalloc(size)
#define RtlKfree(pv)                    vPortFreeAligned(pv)
#endif

#ifdef CONFIG_TIMER_MODULE
#define __Delay(t)                  HalDelayUs(t)
#else
static __inline__ u32 __Delay(u32 us)
{
    printf("No Delay: please enable hardware Timer\n");
    return 0;
}
#endif


#define Mdelay(t)					__Delay(t*1000)
#define Udelay(t)					__Delay(t)

#endif /* __SYS_SUPPORT_H__ */
