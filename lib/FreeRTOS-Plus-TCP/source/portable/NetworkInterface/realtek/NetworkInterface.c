/*****************************************************************************
* @file        NetworkInterface.c
* @brief       Network Interface file for FreeRTOS-Plus-TCP stack.
* 
* @version    V1.3.2
* @date       2018-08-21
*
* @note
*
******************************************************************************
*
* Copyright(c) 2007 - 2018 Realtek Corporation. All rights reserved.
*
* SPDX-License-Identifier: Apache-2.0
*
* Licensed under the Apache License, Version 2.0 (the License); you may
* not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an AS IS BASIS, WITHOUT
* WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
******************************************************************************/

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "list.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_DNS.h"
#include "NetworkBufferManagement.h"
#include "NetworkInterface.h"

/* Ameba includes. */
#include <wireless.h>
#include <skbuff.h>

struct eth_drv_sg {
    unsigned int	buf;
    unsigned int 	len;
};

#define MAX_ETH_DRV_SG	32
#define MAX_ETH_MSG	1540
extern struct sk_buff * rltk_wlan_get_recv_skb(int idx);
extern struct sk_buff * rltk_wlan_alloc_skb(unsigned int total_len);

/* If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 1, then the Ethernet
driver will filter incoming packets and only pass the stack those packets it
considers need processing. */
#if( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eProcessBuffer
#else
#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eConsiderFrameForProcessing( ( pucEthernetBuffer ) )
#endif

enum if_state_t {
    INTERFACE_DOWN = 0,
    INTERFACE_UP,
};
volatile static uint32_t xInterfaceState = INTERFACE_UP;

int aws_wlan_send(int idx, struct eth_drv_sg *sg_list, int sg_len, int total_len)
{
	struct eth_drv_sg *last_sg;
	struct sk_buff *skb = NULL;
	int ret = 0;

	if(idx == -1){
		configPRINTF( ("netif is DOWN") );
		return -1;
	}
	//DBG_TRACE("%s is called", __FUNCTION__);
	
	save_and_cli();
	if(rltk_wlan_check_isup(idx))
		rltk_wlan_tx_inc(idx);
	else {
		configPRINTF( ("netif is DOWN") );
		restore_flags();
		return -1;
	}
	restore_flags();

	skb = rltk_wlan_alloc_skb(total_len);
	if (skb == NULL) {
		//DBG_ERR("rltk_wlan_alloc_skb() for data len=%d failed!", total_len);
		ret = -1;
		goto exit;
	}

	for (last_sg = &sg_list[sg_len]; sg_list < last_sg; ++sg_list) {
		rtw_memcpy(skb->tail, (void *)(sg_list->buf), sg_list->len);
		skb_put(skb,  sg_list->len);		
	}

	rltk_wlan_send_skb(idx, skb);

exit:
	save_and_cli();
	rltk_wlan_tx_dec(idx);
	restore_flags();
	return ret;
}

BaseType_t xNetworkInterfaceInitialise( void )
{
    static BaseType_t xMACAdrInitialized = pdFALSE;
    uint8_t ucMACAddress[ ipMAC_ADDRESS_LENGTH_BYTES ];
    char buf[32];
    char mac[ipMAC_ADDRESS_LENGTH_BYTES];
    int i = 0;
    while(!rltk_wlan_running(0)){
        vTaskDelay(1000);
    }
    if (xInterfaceState == INTERFACE_UP) {
        if (xMACAdrInitialized == pdFALSE) {
            wifi_get_mac_address(buf);
            sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
            for(i=0; i<ipMAC_ADDRESS_LENGTH_BYTES; i++)
                ucMACAddress[i] = mac[i]&0xFF;
            FreeRTOS_UpdateMACAddress(ucMACAddress);
            xMACAdrInitialized = pdTRUE;
        }
        return pdTRUE;
    }
    return pdFALSE;
}

BaseType_t xNetworkInterfaceOutput( NetworkBufferDescriptor_t * const pxNetworkBuffer, BaseType_t xReleaseAfterSend )
{
    BaseType_t ret = pdFALSE;
    struct eth_drv_sg sg_list[MAX_ETH_DRV_SG];
    int sg_len = 0;

    if (pxNetworkBuffer == NULL || pxNetworkBuffer->pucEthernetBuffer == NULL || pxNetworkBuffer->xDataLength == 0) {
        configPRINTF( ("Invalid params") );
        return ret;
    }

    if(!rltk_wlan_running(0))
        return pdFALSE;

    sg_list[sg_len].buf = (unsigned int) pxNetworkBuffer->pucEthernetBuffer;
    sg_list[sg_len++].len = pxNetworkBuffer->xDataLength;

    if (sg_len) {
        if (aws_wlan_send(0, sg_list, sg_len, pxNetworkBuffer->xDataLength) == 0)
            ret = pdTRUE;
    }
exit:
    if (xReleaseAfterSend == pdTRUE) {
        vReleaseNetworkBufferAndDescriptor(pxNetworkBuffer);
    }
    return ret;
}

void vNetworkInterfaceAllocateRAMToBuffers( NetworkBufferDescriptor_t pxNetworkBuffers[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ] )
{
    configPRINTF( ("NOT ready yet\r\n") );
}

BaseType_t xGetPhyLinkStatus( void )
{
    configPRINTF( ("NOT ready yet\r\n") );
    return pdFALSE;
}

void vNetworkInterfaceRecv(int idx, unsigned int total_len)
{
    NetworkBufferDescriptor_t *pxNetworkBuffer;
    IPStackEvent_t xRxEvent = { eNetworkRxEvent, NULL };
    const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS( 250 );
    struct sk_buff *skb;
    if(!rltk_wlan_running(idx))
        return;

    if ((total_len > MAX_ETH_MSG) || (total_len < 0))
        total_len = MAX_ETH_MSG;

    // Allocate buffer to store received packet
    pxNetworkBuffer = pxGetNetworkBufferWithDescriptor(total_len, xDescriptorWaitTime);
    if (pxNetworkBuffer != NULL) {
        if(idx == -1){
            configPRINTF( ("skb is NULL") );
            return;
        }
        skb = rltk_wlan_get_recv_skb(idx);
        configASSERT(skb);
        rtw_memcpy((void *)(pxNetworkBuffer->pucEthernetBuffer), skb->data, total_len);
        skb_pull(skb, total_len);
        if( eConsiderFrameForProcessing( pxNetworkBuffer->pucEthernetBuffer ) != eProcessBuffer ) {
            configPRINTF( ("Dropping packet") );
            return;
        }
        xRxEvent.pvData = (void *) pxNetworkBuffer;
        if ( xSendEventStructToIPTask( &xRxEvent, xDescriptorWaitTime) == pdFAIL ) {
            configPRINTF( ("Failed to enqueue packet to network stack %p, len %d", pxNetworkBuffer->pucEthernetBuffer, total_len) );
            vReleaseNetworkBufferAndDescriptor(pxNetworkBuffer);
            return;
        }
    }
    return;
}

