/*
 * FreeRTOS Wi-Fi V1.0.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file iot_wifi.c
 * @brief Wi-Fi Interface.
 */

/* Socket and Wi-Fi interface includes. */
#include "FreeRTOS.h"
#include "Semphr.h"
#include "iot_wifi.h"

/* Wi-Fi configuration includes. */
#include "aws_wifi_config.h"

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

static int ota_recover = ENABLE;
static int reconnect_count = 0;
/*-----------------------------------------------------------*/

static WIFIEventHandler_t xWifiEventHandlers[ eWiFiEventMax ]; 

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
        case RTW_SECURITY_WEP_SHARED:
            xConvertedSecurityType = eWiFiSecurityWEP;
            break;

        case RTW_SECURITY_WPA_TKIP_PSK:
        case RTW_SECURITY_WPA_AES_PSK :
           xConvertedSecurityType = eWiFiSecurityWPA;
           break;

        case RTW_SECURITY_WPA2_AES_PSK:
        case RTW_SECURITY_WPA2_TKIP_PSK:
        case RTW_SECURITY_WPA2_MIXED_PSK:
        case RTW_SECURITY_WPA_WPA2_MIXED:
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
    WIFIIPConfiguration_t xIPInfo;

    if((pxNetworkParams == NULL) || ((char*)pxNetworkParams->ucSSID == NULL) || (pxNetworkParams->ucSSIDLength == 0))
        return eWiFiFailure;
    if((prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity) != RTW_SECURITY_OPEN) && ((char*)pxNetworkParams->xPassword.xWPA.cPassphrase == NULL))
        return eWiFiFailure;

    rtw_network_info_t connect_param = {0};
    memcpy(connect_param.ssid.val, (char*)pxNetworkParams->ucSSID, pxNetworkParams->ucSSIDLength);
	connect_param.ssid.len = pxNetworkParams->ucSSIDLength;
	connect_param.password = (char*)pxNetworkParams->xPassword.xWPA.cPassphrase;
	connect_param.password_len = pxNetworkParams->xPassword.xWPA.ucLength;
	connect_param.security_type = prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity);

    printf("\n\rJoining BSS by SSID %s...\n\r", (char*)pxNetworkParams->ucSSID);

    device_mutex_lock(RT_DEV_LOCK_WLAN);
	if(pxNetworkParams->xPassword.xWPA.cPassphrase != NULL)
	{
        ret = wifi_connect(&connect_param, 1);
    }
	else
	{
        connect_param.password_len = 0;
        ret = wifi_connect(&connect_param, 1);
    }
    device_mutex_unlock(RT_DEV_LOCK_WLAN);

    if(ret!= RTW_SUCCESS){
        if(ret == RTW_INVALID_KEY)
            printf("\n\rERROR:Invalid Key ");

        printf("\n\rERROR: Can't connect to AP");
        ret = eWiFiFailure;
    }
    else
    {
		reconnect_count=0;
#if CONFIG_LWIP_LAYER
		LwIP_DHCP(0, DHCP_START);
#endif
		//WIFI_GetIP((uint8_t *)&ipaddr);
        WIFI_GetIPInfo(&xIPInfo);
		while(xIPInfo.xIPAddress.ulAddress == NULL) {
			printf("\n\rWaiting for IP address");
			WIFI_GetIPInfo(&xIPInfo);
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

static rtw_result_t scan_result_handler(unsigned int scanned_AP_num, void *user_data)
{
	/* To avoid gcc warnings */
	(void) user_data;

	rtw_scan_result_t *scanned_ap_info;
	char *scan_buf = NULL;
	int i = 0;

	if (scanned_AP_num == 0) {
		return RTW_ERROR;
	}

	scan_buf = (char *)rtw_zmalloc(scanned_AP_num * sizeof(rtw_scan_result_t));
	if (scan_buf == NULL) {
		printf("malloc scan buf fail for example_wlan_scenario\n");
		return RTW_ERROR;
	}

	if (wifi_get_scan_records(&scanned_AP_num, scan_buf) < 0) {
		rtw_mfree(scan_buf, 0);
		return RTW_ERROR;
	}

	for (i = 0; i < scanned_AP_num; i++) {
		scanned_ap_info = (rtw_scan_result_t *)(scan_buf + i * sizeof(rtw_scan_result_t));
		scanned_ap_info->SSID.val[scanned_ap_info->SSID.len] = 0; /* Ensure the SSID is null terminated */

		printf("%d\t", (i + 1));
		printf("%s\t ", (scanned_ap_info->bss_type == RTW_BSS_TYPE_ADHOC) ? "Adhoc" : "Infra");
		printf("%02x:%02x:%02x:%02x:%02x:%02x", MAC_ARG(scanned_ap_info->BSSID.octet));
		printf(" %d\t ", scanned_ap_info->signal_strength);
		printf(" %d\t  ", scanned_ap_info->channel);
		printf(" %d\t  ", scanned_ap_info->wps_type);
		printf("%s\t\t ", (scanned_ap_info->security == RTW_SECURITY_OPEN) ? "Open" :
			   (scanned_ap_info->security == RTW_SECURITY_WEP_PSK) ? "WEP" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA_TKIP_PSK) ? "WPA TKIP" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA_AES_PSK) ? "WPA AES" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA2_AES_PSK) ? "WPA2 AES" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA2_TKIP_PSK) ? "WPA2 TKIP" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA2_MIXED_PSK) ? "WPA2 Mixed" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA_WPA2_MIXED) ? "WPA/WPA2 AES" :
			   (scanned_ap_info->security == RTW_SECURITY_WPA3_AES_PSK) ? "WPA3 AES" :
			   "Unknown");
		printf(" %s ", scanned_ap_info->SSID.val);
		printf("\r\n");
	}
	rtw_mfree(scan_buf, 0);
	return RTW_SUCCESS;
}

WIFIReturnCode_t WIFI_Scan( WIFIScanResult_t * pxBuffer,
                            uint8_t ucNumNetworks )
{
    /* Check params */
    if ((NULL == pxBuffer) || (0 == ucNumNetworks))
        return eWiFiFailure;

    // Scan network and print them out.
	// Can refer to fATWS() in atcmd_wifi.c and scan_result_handler() below.
	printf("\n\rScan network\n");
	rtw_scan_param_t scan_param;
	rtw_memset(&scan_param, 0, sizeof(scan_param));
	scan_param.scan_user_callback = scan_result_handler;
	if (wifi_scan_networks(&scan_param, 0) != RTW_SUCCESS) {
		printf("\n\rERROR: wifi_scan_networks() failed\n");
        goto exit;
    }
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

WIFIReturnCode_t WIFI_GetIPInfo( WIFIIPConfiguration_t * pxIPInfo )
{
    if(pxIPInfo == NULL)
        return eWiFiFailure;
    
    memset( pxIPInfo, 0x00, sizeof( WIFIIPConfiguration_t ) );
#if (CONFIG_LWIP_LAYER == 1)
    memcpy(&pxIPInfo->xIPAddress.ulAddress[0], (uint32_t*)LwIP_GetIP(&xnetif[0]), 4);
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

    if((pxNetworkParams == NULL) || (pxNetworkParams->ucSSID == NULL) || (pxNetworkParams->xPassword.xWPA.cPassphrase == NULL))
    	return eWiFiFailure;

    if((pxNetworkParams->ucSSIDLength > wificonfigMAX_SSID_LEN) || pxNetworkParams->xPassword.xWPA.ucLength > wificonfigMAX_PASSPHRASE_LEN)
    	return eWiFiFailure;

    if( (wifi_mode != RTW_MODE_AP) ){
        WIFI_SetMode(eWiFiModeAP);
    }
    rtw_softap_info_t softAP_config = {0};
	softAP_config.ssid.len = pxNetworkParams->ucSSIDLength;
	rtw_memcpy(softAP_config.ssid.val, (char*)pxNetworkParams->ucSSID, softAP_config.ssid.len);
	softAP_config.password = (char*)pxNetworkParams->xPassword.xWPA.cPassphrase;
	softAP_config.password_len = pxNetworkParams->xPassword.xWPA.ucLength;
	softAP_config.channel = pxNetworkParams->ucChannel;
	softAP_config.security_type = prvConvertSecurityAbstractedToRTW(pxNetworkParams->xSecurity);
	if (wifi_start_ap(&softAP_config) < 0) {
        printf("\n\r[WLAN_SCENARIO_EXAMPLE] ERROR: wifi_start_ap failed\n");
        return eWiFiFailure;
    }

    while(1) {
        char essid[33];
        int timeout = 20;
        if(wext_get_ssid(WLAN0_NAME, (unsigned char *) essid) > 0) {
            if(strncmp((const char *) essid, (const char *)pxNetworkParams->ucSSID, pxNetworkParams->ucSSIDLength) == 0) {
                printf("\n\r%s started\n", pxNetworkParams->ucSSID);
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

BaseType_t WIFI_IsConnected( const WIFINetworkParams_t * pxNetworkParams )
{
    if (wifi_is_connected_to_ap() == RTW_SUCCESS)
        return pdTRUE;
	return pdFALSE;
}
/*-----------------------------------------------------------*/

WIFIReturnCode_t WIFI_RegisterEvent( WIFIEventType_t xEventType,
                                     WIFIEventHandler_t xHandler )
{
    WIFIReturnCode_t xWiFiRet;

    switch( xEventType )
    {
        case eWiFiEventIPReady:
        case eWiFiEventDisconnected:
            xWifiEventHandlers[ xEventType ] = xHandler;
            xWiFiRet = eWiFiSuccess;
            break;
        default:
            xWiFiRet = eWiFiNotSupported;
            break;
    }


    return xWiFiRet;
}
/*-----------------------------------------------------------*/
