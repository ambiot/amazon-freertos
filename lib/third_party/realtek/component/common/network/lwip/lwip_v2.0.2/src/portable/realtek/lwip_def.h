#ifndef LWIP_DEF_H
#define LWIP_DEF_H


#include "lwip/opt.h"
#include "lwip/def.h"

#include "lwip/ip4_addr.h"
#include "lwip/ip6_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LWIP_IPV4
/* Added by Realtek start */
#define ip_addr ip4_addr
/* Added by Realtek end */
#endif

//Realtek add 
struct sys_timeouts {
  struct sys_timeo *next;
};

struct sys_timeouts *sys_arch_timeouts(void);
//Realtek add end












#endif