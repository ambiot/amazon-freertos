/*****************************************************************************
* @file        aws_wifi.c
* @brief       Amazon FreeRTOS Wi-Fi for Realtek Ameba.
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

/**
 * @file aws_wifi.c
 * @brief Wi-Fi Interface.
 */

/* Socket and Wi-Fi interface includes. */
#include "FreeRTOS.h"
#include "Semphr.h"
#include "aws_wifi.h"

/* Wi-Fi configuration includes. */
#include "aws_wifi_config.h"

#include "aws_secure_sockets.h"

#include "wifi_constants.h"
#include "wifi_structures.h"
#include "wifi_conf.h"
#include "device_lock.h"

#if CONFIG_LWIP_LAYER
#include "lwip_intf.h"
#include "lwip_netconf.h"
extern struct netif xnetif[NET_IF_NUM];
#endif

extern rtw_mode_t wifi_mode;

typedef struct {
    WIFIScanResult_t * pxBuffer;
    uint8_t ucNumNetworks;
    SemaphoreHandle_t xScanSemaphore;
} WIFIScanParam_t;

/*-----------------------------------------------------------*/

static rtw_security_t prvConvertSecurityAbstractedToRTW( WIFISecurity_t xSecurity )
{
    rtw_security_t ucConvertedSecurityType = RTW_SECURITY_UNKNOWN;

    switch( xSecurity )
    {
        case eWiFiSecurityOpen:
            ucConvertedSecurityType = RTW_SECURITY_OPEN;
            break;

        case eWiFiSecurityWEP:
            ucConvertedSecurityType = RTW_SECURITY_WEP_PSK;
            break;

        case eWiFiSecurityWPA:
            ucConvertedSecurityType = RTW_SECURITY_WPA_AES_PSK;
            break;

        case eWiFiSecurityWPA2:
            ucConvertedSecurityType = RTW_SECURITY_WPA2_AES_PSK;
            break;

        default:
            break;
    }

    return ucConvertedSecurityType;
}

/*-----------------------------------------------------------*/

static WIFISecurity_t prvConvertSecurityRTWToAbstracted( rtw_security_t ucSecurity )
{
    WIFISecurity_t xConvertedSecurityType = eWiFiSecurityNotSupported;

    switch( ucSecurity )
    {
        case RTW_SECURITY_OPEN:
            xConvertedSecurityType = eWiFiSecurityOpen;
            break;

        case RTW_SECURITY_WEP_PSK:
            xConvertedSecurityType = eWiFiSecurityWEP;
            break;

        case RTW_SECURITY_WPA_AES_PSK :
           xConvertedSecurityType = eWiFiSecurityWPA;
           break;

        case RTW_SECURITY_WPA2_MIXED_PSK:
            xConvertedSecurityType = eWiFiSecurityWPA2;
            break;

        default:
            break;
    }

    return xConvertedSecurityType;
}

/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_On( void )
{
#if CONFIG_INIT_NET
#if CONFIG_LWIP_LAYER
extern int lwip_init_done; 
    if(lwip_init_done == 0){
        LwIP_Init();
    }
#endif
#endif
#if CONFIG_WIFI_IND_USE_THREAD
    wifi_manager_init();
#endif
    wifi_off();
    vTaskDelay(20);
    if (wifi_on(RTW_MODE_STA) < 0){
        printf("Wifi on failed!");
        return eWiFiFailure;
    }
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Off( void )
{
    wifi_off();
    vTaskDelay(20);
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_ConnectAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    int ret = eWiFiFailure;
    uint32_t ipaddr = 0;

    if((pxNetworkParams == NULL) || ((char*)pxNetworkParams->pcSSID == NULL))
    	return eWiFiFailure;
    if((prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity) != RTW_SECURITY_OPEN) && ((char*)pxNetworkParams->pcPassword == NULL))
        return eWiFiFailure;

    device_mutex_lock(RT_DEV_LOCK_WLAN);
    printf("\n\rJoining BSS by SSID %s...\n\r", (char*)pxNetworkParams->pcSSID);

    ret = wifi_connect((char*)pxNetworkParams->pcSSID, prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity), (char*)pxNetworkParams->pcPassword,
    					strlen((char*)pxNetworkParams->pcSSID), strlen((char*)pxNetworkParams->pcPassword), -1, NULL);

    device_mutex_unlock(RT_DEV_LOCK_WLAN);

    if(ret!= RTW_SUCCESS){
        if(ret == RTW_INVALID_KEY)
            printf("\n\rERROR:Invalid Key ");

        printf("\n\rERROR: Can't connect to AP");
        ret = eWiFiFailure;
    }
    else
    {

#if CONFIG_LWIP_LAYER
		LwIP_DHCP(0, DHCP_START);
#endif
		WIFI_GetIP((uint8_t *)&ipaddr);
		while(ipaddr == 0) {
			printf("\n\rWaiting for IP address");
			WIFI_GetIP((uint8_t *)&ipaddr);
			vTaskDelay(1000);
		}
		ret = eWiFiSuccess;
    }

    return ret;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Disconnect( void )
{
    wifi_disconnect();
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Reset( void )
{
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

static rtw_result_t aws_scan_result_handler( rtw_scan_handler_result_t* malloced_scan_result )
{
    static int ApNum = 0;
    WIFIScanParam_t *scan_param;
    if (malloced_scan_result->scan_complete != RTW_TRUE) {
        rtw_scan_result_t* record = &malloced_scan_result->ap_details;
        record->SSID.val[record->SSID.len] = 0; /* Ensure the SSID is null terminated */
        //RTW_API_INFO( ( "%d\t ", ++ApNum ) );
        //print_scan_result(record);
        if(malloced_scan_result->user_data) {
            scan_param = (WIFIScanParam_t *) malloced_scan_result->user_data;
            if (ApNum < scan_param->ucNumNetworks) {
                WIFIScanResult_t *pxBuffer = scan_param->pxBuffer;
                strncpy(pxBuffer[ApNum].cSSID, record->SSID.val, wificonfigMAX_SSID_LEN);
                strncpy(pxBuffer[ApNum].ucBSSID, record->BSSID.octet, wificonfigMAX_BSSID_LEN);
                pxBuffer[ApNum].cRSSI = record->signal_strength;
                pxBuffer[ApNum].cChannel = record->signal_strength;
                pxBuffer[ApNum].ucHidden = 0;
                pxBuffer[ApNum].xSecurity = prvConvertSecurityRTWToAbstracted(record->security);
            }
        }
        ++ApNum;
    } else{
        ApNum = 0;
        if(malloced_scan_result->user_data) {
            scan_param = (WIFIScanParam_t *) malloced_scan_result->user_data;
            rtw_up_sema(&scan_param->xScanSemaphore);
        }
    }
    return RTW_SUCCESS;
}

WIFIReturnCode_t WIFI_Scan( WIFIScanResult_t * pxBuffer,
                            uint8_t ucNumNetworks )
{
    int ret;
    static WIFIScanParam_t scan_param = {0};
    rtw_bool_t result;
    /* Check params */
    if ((NULL == pxBuffer) || (0 == ucNumNetworks))
        return eWiFiFailure;

    /* Acquire semaphore */
    //if (xSemaphoreTake(g_wifi_semaph, portMAX_DELAY) == pdTRUE)
    //{}
    if (scan_param.xScanSemaphore== NULL)
        rtw_init_sema(&scan_param.xScanSemaphore, 0);
    scan_param.pxBuffer = pxBuffer;
    scan_param.ucNumNetworks = ucNumNetworks;
    if((ret = wifi_scan_networks(aws_scan_result_handler, &scan_param)) != RTW_SUCCESS){
        printf("wifi scan failed\n\r");
        goto exit;
    }
    rtw_down_timeout_sema(&scan_param.xScanSemaphore, 5000);
    return eWiFiSuccess;
exit:
    return eWiFiFailure;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_SetMode( WIFIDeviceMode_t xDeviceMode )
{
	int ret = eWiFiSuccess;
    switch (xDeviceMode) 
    {
        case eWiFiModeStation:
            wifi_off();
            vTaskDelay(20);
            if (wifi_on(RTW_MODE_STA) < 0){
                printf("\n\rERROR: Wifi on failed!");
            }
            break;
        case eWiFiModeAP:
            wifi_off();
            vTaskDelay(20);
            if (wifi_on(RTW_MODE_AP) < 0){
                printf("\n\rERROR: Wifi on failed!");
            }
            break;
        case eWiFiModeP2P:
            wifi_off();
            vTaskDelay(20);
            if (wifi_on(RTW_MODE_P2P) < 0){
                printf("\n\rERROR: Wifi on failed!");
            }
            break;
        default:
        	ret = eWiFiFailure;
            break;
    }

    return ret;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetMode( WIFIDeviceMode_t * pxDeviceMode )
{
    int mode, ret;
    if(pxDeviceMode == NULL)
    	return eWiFiFailure;
    ret = wext_get_mode("wlan0", &mode);
    if (ret < 0)
        return eWiFiFailure;
    switch (wifi_mode) 
    {
        case RTW_MODE_STA:
            * pxDeviceMode = eWiFiModeStation;
            break;
        case RTW_MODE_AP:
            * pxDeviceMode = eWiFiModeAP;
            break;
        case RTW_MODE_P2P:
            * pxDeviceMode = eWiFiModeP2P;
            break;
        default:
            return eWiFiPMNotSupported;
    }
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_NetworkAdd( const WIFINetworkProfile_t * const pxNetworkProfile,
                                  uint16_t * pusIndex )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_NetworkGet( WIFINetworkProfile_t * pxNetworkProfile,
                                  uint16_t usIndex )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_NetworkDelete( uint16_t usIndex )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_Ping( uint8_t * pucIPAddr,
                            uint16_t usCount,
                            uint32_t ulIntervalMS )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetIP( uint8_t * pucIPAddr )
{
	if(pucIPAddr == NULL)
		return eWiFiFailure;
#if (CONFIG_LWIP_LAYER == 1)
    uint8_t *ip = (uint8_t *)LwIP_GetIP(&xnetif[0]);
    memcpy(pucIPAddr, ip, 4);
#else
     *( ( uint32_t * ) pucIPAddr ) = FreeRTOS_GetIPAddress();
#endif
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetMAC( uint8_t * pucMac )
{
    char buf[32];
    char mac[wificonfigMAX_BSSID_LEN];
    int i = 0;

    if(pucMac == NULL)
    	return eWiFiFailure;

    wifi_get_mac_address(buf);
    sscanf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    for(i=0; i<wificonfigMAX_BSSID_LEN; i++)
        pucMac[i] = mac[i]&0xFF;
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetHostIP( char * pcHost,
                                 uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    uint32_t IPAddr;

    if((pcHost == NULL)||(pucIPAddr == NULL))
    	return eWiFiFailure;

    IPAddr = SOCKETS_GetHostByName(pcHost);
    if (IPAddr != 0UL)
    {
        *( ( uint32_t * ) pucIPAddr ) = IPAddr;
        xRetVal = eWiFiSuccess;
    }
    return xRetVal;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_StartAP( void )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_StopAP( void )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_ConfigureAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    int ret;
#if CONFIG_LWIP_LAYER
    struct netif * pnetif = &xnetif[0];
#endif

    if((pxNetworkParams == NULL) || (pxNetworkParams->pcSSID == NULL) || (pxNetworkParams->pcPassword == NULL))
    	return eWiFiFailure;

    if((strlen((char*)pxNetworkParams->pcSSID) > wificonfigMAX_SSID_LEN) || (strlen((char*)pxNetworkParams->pcPassword) > wificonfigMAX_PASSPHRASE_LEN))
    	return eWiFiFailure;

    if( (wifi_mode != RTW_MODE_AP) ){
        WIFI_SetMode(eWiFiModeAP);
    }
    if((ret = wifi_start_ap((char*)pxNetworkParams->pcSSID, prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity), 
                                            (char*)pxNetworkParams->pcPassword, strlen((char*)pxNetworkParams->pcSSID), strlen((char*)pxNetworkParams->pcPassword),
                                            pxNetworkParams->cChannel) )< 0) {
        printf("\n\rERROR: Operation failed!");
        return eWiFiFailure;
    }

    while(1) {
        char essid[33];
        int timeout = 20;
        if(wext_get_ssid(WLAN0_NAME, (unsigned char *) essid) > 0) {
            if(strncmp((const char *) essid, (const char *)pxNetworkParams->pcSSID, pxNetworkParams->ucSSIDLength) == 0) {
                printf("\n\r%s started\n", pxNetworkParams->pcSSID);
                break;
            }
        }

        if(timeout == 0) {
            printf("\n\rERROR: Start AP timeout!");
            return eWiFiTimeout;		
        }

        vTaskDelay(1 * configTICK_RATE_HZ);
        timeout --;
    }
#if defined( CONFIG_ENABLE_AP_POLLING_CLIENT_ALIVE )&&( CONFIG_ENABLE_AP_POLLING_CLIENT_ALIVE == 1 )
    wifi_set_ap_polling_sta(1);
#endif

#if CONFIG_LWIP_LAYER
    //LwIP_UseStaticIP(pnetif);
    dhcps_init(pnetif);
#endif
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_SetPMMode( WIFIPMMode_t xPMModeType,
                                 const void * pvOptionValue )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_GetPMMode( WIFIPMMode_t * pxPMModeType,
                                 void * pvOptionValue )
{
    /* FIX ME. */
    return eWiFiNotSupported;
}
/*-----------------------------------------------------------*/

BaseType_t WIFI_IsConnected(void)
{
    if (wifi_is_connected_to_ap() == RTW_SUCCESS)
        return pdTRUE;
    return pdFALSE;
}
