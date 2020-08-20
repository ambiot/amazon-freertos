/**
*****************************************************************************************
*     Copyright(c) 2017, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
   * @file      main.c
   * @brief     Source file for BLE central client project, mainly used for initialize modules
   * @author    jane
   * @date      2017-06-12
   * @version   v1.0
   **************************************************************************************
   * @attention
   * <h2><center>&copy; COPYRIGHT 2017 Realtek Semiconductor Corporation</center></h2>
   **************************************************************************************
  */

/*============================================================================*
 *                              Header Files
 *============================================================================*/
#include <os_sched.h>
#include <string.h>
#include <ble_scatternet_app_task.h>
#include <trace_app.h>
#include <gap.h>
#include <gap_config.h>
#include <gap_bond_le.h>
#include <gap_scan.h>
#include <profile_client.h>
#include <gap_msg.h>
#include <ble_scatternet_app.h>
#include <gcs_client.h>
#include <ble_scatternet_link_mgr.h>
#include "trace_uart.h"
#include <bte.h>

#include "wifi_constants.h"
#include "FreeRTOS.h"
#include <profile_server.h>
#include <gap_adv.h>
#include <gap_le_types.h>
#include <simple_ble_config.h>
#include <gatt_builtin_services.h>
#include <wifi/wifi_conf.h>
#include "bas.h"
#include "task.h"
#include "rtk_coex.h"
#include <simple_ble_service.h>
#include "platform_opts_bt.h"

#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
#include <gap_adv.h>
#include <gap_conn_le.h>
#include <profile_server.h>
#include <bt_flags.h>
#include "bt_config_app_main.h"
#include "bt_config_wifi.h"
#include "bt_config_service.h"
#include "bt_config_app_flags.h"
#include "bt_config_app_task.h"
#include "bt_config_peripheral_app.h"
#include "bt_config_config.h"
#include "lwip_netconf.h"

extern uint8_t bt_config_conn_id;
extern T_GAP_CONN_STATE bt_config_gap_conn_state;
extern uint8_t airsync_specific;
extern void bt_config_app_set_adv_data(void);
#endif

/** @defgroup  CENTRAL_CLIENT_DEMO_MAIN Central Client Main
    * @brief Main file to initialize hardware and BT stack and start task scheduling
    * @{
    */

/*============================================================================*
 *                              Constants
 *============================================================================*/
/** @brief Default scan interval (units of 0.625ms, 0x520=820ms) */
#define DEFAULT_SCAN_INTERVAL     0x520
/** @brief Default scan window (units of 0.625ms, 0x520=820ms) */
#define DEFAULT_SCAN_WINDOW       0x520

extern T_SERVER_ID simp_srv_id; /**< Simple ble service id*/
extern T_SERVER_ID bas_srv_id;  /**< Battery service id */
extern T_APP_RESULT app_profile_callback(T_SERVER_ID service_id, void *p_data);
extern bool bt_trace_uninit(void);
/** @brief  GAP - scan response data (max size = 31 bytes) */
static const uint8_t scan_rsp_data[] =
{
    0x03,                             /* length */
    GAP_ADTYPE_APPEARANCE,            /* type="Appearance" */
    LO_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
    HI_WORD(GAP_GATT_APPEARANCE_UNKNOWN),
};

/** @brief  GAP - Advertisement data (max size = 31 bytes, best kept short to conserve power) */
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)  
static const uint8_t adv_data[] =
{
    /* Flags */
    0x02,             /* length */
    GAP_ADTYPE_FLAGS, /* type="Flags" */
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    /* Service */
    0x03,             /* length */
    GAP_ADTYPE_16BIT_COMPLETE,
    LO_WORD(GATT_UUID_BT_CONFIG_PROFILE),
    HI_WORD(GATT_UUID_BT_CONFIG_PROFILE),
    /* Local name */
    0x0D,             /* length */
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'A', 'm', 'e', 'b', 'a', '_', 'x', 'x', 'y', 'y', 'z', 'z',
};
#else
static const uint8_t adv_data[] =
{
    /* Flags */
    0x02,             /* length */
    GAP_ADTYPE_FLAGS, /* type="Flags" */
    GAP_ADTYPE_FLAGS_LIMITED | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    /* Service */
    0x03,             /* length */
    GAP_ADTYPE_16BIT_COMPLETE,
    LO_WORD(GATT_UUID_SIMPLE_PROFILE),
    HI_WORD(GATT_UUID_SIMPLE_PROFILE),
    /* Local name */
    0x0F,             /* length */
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'B', 'L', 'E', '_', 'S', 'C', 'A', 'T', 'T', 'E', 'R', 'N', 'E', 'T',
};
#endif

/** @brief  Default minimum advertising interval when device is discoverable (units of 625us, 160=100ms) */
#define DEFAULT_ADVERTISING_INTERVAL_MIN            352 //220ms
/** @brief  Default maximum advertising interval */
#define DEFAULT_ADVERTISING_INTERVAL_MAX            384 //240ms

//extern T_SERVER_ID simp_srv_id; /**< Simple ble service id*/
//extern T_SERVER_ID bas_srv_id;  /**< Battery service id */

/*============================================================================*
 *                              Functions
 *============================================================================*/
/**
 * @brief  Config bt stack related feature
 *
 * NOTE: This function shall be called before @ref bte_init is invoked.
 * @return void
 */
void ble_scatternet_bt_stack_config_init(void)
{
    gap_config_max_le_link_num(BLE_SCATTERNET_APP_MAX_LINKS);
}

/**
  * @brief  Initialize central and gap bond manager related parameters
  * @return void
  */
void ble_scatternet_app_le_gap_init(void)
{
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG) 
    /* Device name and device appearance */
    uint8_t  device_name[GAP_DEVICE_NAME_LEN] = "Ameba_xxyyzz";
    uint16_t appearance = GAP_GATT_APPEARANCE_UNKNOWN;
    uint8_t  slave_init_mtu_req = true;
#else
	/* Device name and device appearance */
    uint8_t  device_name[GAP_DEVICE_NAME_LEN] = "BLE_SCATTERNET";
    uint16_t appearance = GAP_GATT_APPEARANCE_UNKNOWN;
#endif

    /* Advertising parameters */
    uint8_t  adv_evt_type = GAP_ADTYPE_ADV_IND;
    uint8_t  adv_direct_type = GAP_REMOTE_ADDR_LE_PUBLIC;
    uint8_t  adv_direct_addr[GAP_BD_ADDR_LEN] = {0};
    uint8_t  adv_chann_map = GAP_ADVCHAN_ALL;
    uint8_t  adv_filter_policy = GAP_ADV_FILTER_ANY;
    uint16_t adv_int_min = DEFAULT_ADVERTISING_INTERVAL_MIN;
    uint16_t adv_int_max = DEFAULT_ADVERTISING_INTERVAL_MAX;

    /* Scan parameters */
    uint8_t  scan_mode = GAP_SCAN_MODE_ACTIVE;
    uint16_t scan_interval = DEFAULT_SCAN_INTERVAL;
    uint16_t scan_window = DEFAULT_SCAN_WINDOW;
    uint8_t  scan_filter_policy = GAP_SCAN_FILTER_ANY;
    uint8_t  scan_filter_duplicate = GAP_SCAN_FILTER_DUPLICATE_ENABLE;

    /* GAP Bond Manager parameters */
    uint8_t  auth_pair_mode = GAP_PAIRING_MODE_PAIRABLE;
    uint16_t auth_flags = GAP_AUTHEN_BIT_BONDING_FLAG;
    uint8_t  auth_io_cap = GAP_IO_CAP_NO_INPUT_NO_OUTPUT;
#if F_BT_LE_SMP_OOB_SUPPORT
    uint8_t  auth_oob = false;
#endif
    uint8_t  auth_use_fix_passkey = false;
    uint32_t auth_fix_passkey = 0;
    uint8_t  auth_sec_req_enable = false;
    uint16_t auth_sec_req_flags = GAP_AUTHEN_BIT_BONDING_FLAG;

    /* Set device name and device appearance */
    le_set_gap_param(GAP_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, device_name);
    le_set_gap_param(GAP_PARAM_APPEARANCE, sizeof(appearance), &appearance);
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG) 
	le_set_gap_param(GAP_PARAM_SLAVE_INIT_GATT_MTU_REQ, sizeof(slave_init_mtu_req),
                     &slave_init_mtu_req);
#endif

	/* Set advertising parameters */
	le_adv_set_param(GAP_PARAM_ADV_EVENT_TYPE, sizeof(adv_evt_type), &adv_evt_type);
	le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR_TYPE, sizeof(adv_direct_type), &adv_direct_type);
	le_adv_set_param(GAP_PARAM_ADV_DIRECT_ADDR, sizeof(adv_direct_addr), adv_direct_addr);
	le_adv_set_param(GAP_PARAM_ADV_CHANNEL_MAP, sizeof(adv_chann_map), &adv_chann_map);
	le_adv_set_param(GAP_PARAM_ADV_FILTER_POLICY, sizeof(adv_filter_policy), &adv_filter_policy);
	le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MIN, sizeof(adv_int_min), &adv_int_min);
	le_adv_set_param(GAP_PARAM_ADV_INTERVAL_MAX, sizeof(adv_int_max), &adv_int_max);
	le_adv_set_param(GAP_PARAM_ADV_DATA, sizeof(adv_data), (void *)adv_data);
	le_adv_set_param(GAP_PARAM_SCAN_RSP_DATA, sizeof(scan_rsp_data), (void *)scan_rsp_data);

    /* Set scan parameters */
    le_scan_set_param(GAP_PARAM_SCAN_MODE, sizeof(scan_mode), &scan_mode);
    le_scan_set_param(GAP_PARAM_SCAN_INTERVAL, sizeof(scan_interval), &scan_interval);
    le_scan_set_param(GAP_PARAM_SCAN_WINDOW, sizeof(scan_window), &scan_window);
    le_scan_set_param(GAP_PARAM_SCAN_FILTER_POLICY, sizeof(scan_filter_policy),
                      &scan_filter_policy);
    le_scan_set_param(GAP_PARAM_SCAN_FILTER_DUPLICATES, sizeof(scan_filter_duplicate),
                      &scan_filter_duplicate);

    /* Setup the GAP Bond Manager */
    gap_set_param(GAP_PARAM_BOND_PAIRING_MODE, sizeof(auth_pair_mode), &auth_pair_mode);
    gap_set_param(GAP_PARAM_BOND_AUTHEN_REQUIREMENTS_FLAGS, sizeof(auth_flags), &auth_flags);
    gap_set_param(GAP_PARAM_BOND_IO_CAPABILITIES, sizeof(auth_io_cap), &auth_io_cap);
#if F_BT_LE_SMP_OOB_SUPPORT
    gap_set_param(GAP_PARAM_BOND_OOB_ENABLED, sizeof(auth_oob), &auth_oob);
#endif
    le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY, sizeof(auth_fix_passkey), &auth_fix_passkey);
    le_bond_set_param(GAP_PARAM_BOND_FIXED_PASSKEY_ENABLE, sizeof(auth_use_fix_passkey),
                      &auth_use_fix_passkey);
    le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_ENABLE, sizeof(auth_sec_req_enable), &auth_sec_req_enable);
    le_bond_set_param(GAP_PARAM_BOND_SEC_REQ_REQUIREMENT, sizeof(auth_sec_req_flags),
                      &auth_sec_req_flags);

    /* register gap message callback */
    le_register_app_cb(ble_scatternet_app_gap_callback);
#if F_BT_LE_USE_STATIC_RANDOM_ADDR
		T_APP_STATIC_RANDOM_ADDR random_addr;
		bool gen_addr = true;
		uint8_t local_bd_type = GAP_LOCAL_ADDR_LE_RANDOM;
		if (ble_scatternet_app_load_static_random_address(&random_addr) == 0)
		{
			if (random_addr.is_exist == true)
			{
				gen_addr = false;
			}
		}
		if (gen_addr)
		{
			if (le_gen_rand_addr(GAP_RAND_ADDR_STATIC, random_addr.bd_addr) == GAP_CAUSE_SUCCESS)
			{
				random_addr.is_exist = true;
				ble_scatternet_app_save_static_random_address(&random_addr);
			}
		}
		le_cfg_local_identity_address(random_addr.bd_addr, GAP_IDENT_ADDR_RAND);
		le_set_gap_param(GAP_PARAM_RANDOM_ADDR, 6, random_addr.bd_addr);
		//only for peripheral,broadcaster
		le_adv_set_param(GAP_PARAM_ADV_LOCAL_ADDR_TYPE, sizeof(local_bd_type), &local_bd_type);
		//only for central,observer
		le_scan_set_param(GAP_PARAM_SCAN_LOCAL_ADDR_TYPE, sizeof(local_bd_type), &local_bd_type);
#endif
#if F_BT_GAPS_CHAR_WRITEABLE
		uint8_t appearance_prop = GAPS_PROPERTY_WRITE_ENABLE;
		uint8_t device_name_prop = GAPS_PROPERTY_WRITE_ENABLE;
		T_LOCAL_APPEARANCE appearance_local;
		T_LOCAL_NAME local_device_name;
		if (flash_load_local_appearance(&appearance_local) == 0)
		{
			gaps_set_parameter(GAPS_PARAM_APPEARANCE, sizeof(uint16_t), &appearance_local.local_appearance);
		}
	
		if (flash_load_local_name(&local_device_name) == 0)
		{
			gaps_set_parameter(GAPS_PARAM_DEVICE_NAME, GAP_DEVICE_NAME_LEN, local_device_name.local_name);
		}
		gaps_set_parameter(GAPS_PARAM_APPEARANCE_PROPERTY, sizeof(appearance_prop), &appearance_prop);
		gaps_set_parameter(GAPS_PARAM_DEVICE_NAME_PROPERTY, sizeof(device_name_prop), &device_name_prop);
		gatt_register_callback((void*)ble_scatternet_gap_service_callback);
#endif
#if F_BT_LE_5_0_SET_PHY_SUPPORT
		uint8_t  phys_prefer = GAP_PHYS_PREFER_ALL;
		uint8_t  tx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
								  GAP_PHYS_PREFER_CODED_BIT;
		uint8_t  rx_phys_prefer = GAP_PHYS_PREFER_1M_BIT | GAP_PHYS_PREFER_2M_BIT |
								  GAP_PHYS_PREFER_CODED_BIT;
		le_set_gap_param(GAP_PARAM_DEFAULT_PHYS_PREFER, sizeof(phys_prefer), &phys_prefer);
		le_set_gap_param(GAP_PARAM_DEFAULT_TX_PHYS_PREFER, sizeof(tx_phys_prefer), &tx_phys_prefer);
		le_set_gap_param(GAP_PARAM_DEFAULT_RX_PHYS_PREFER, sizeof(rx_phys_prefer), &rx_phys_prefer);
#endif

}

/**
 * @brief  Add GATT clients and register callbacks
 * @return void
 */
void ble_scatternet_app_le_profile_init(void)
{
    client_init(1);
    ble_scatternet_gcs_client_id = gcs_add_client(ble_scatternet_gcs_client_callback, BLE_SCATTERNET_APP_MAX_LINKS, BLE_SCATTERNET_APP_MAX_DISCOV_TABLE_NUM);

#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
	server_init(1); 
	bt_config_srv_id = bt_config_service_add_service((void *)bt_config_app_profile_callback);
	server_register_app_cb(bt_config_app_profile_callback);
#else
	server_init(2);
	simp_srv_id = simp_ble_service_add_service((void *)app_profile_callback);
	bas_srv_id	= bas_add_service((void *)app_profile_callback);
	server_register_app_cb(app_profile_callback);
#endif

}


/**
 * @brief    Contains the initialization of all tasks
 * @note     There is only one task in BLE Central Client APP, thus only one APP task is init here
 * @return   void
 */
void ble_scatternet_task_init(void)
{
    ble_scatternet_app_task_init();
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
	airsync_specific = 0;
	bt_config_wifi_init();
#endif
}

/**
 * @brief    Entry of APP code
 * @return   int (To avoid compile warning)
 */
int ble_scatternet_app_main(void)
{
    bt_trace_init();
    ble_scatternet_bt_stack_config_init();
    bte_init();
    le_gap_init(BLE_SCATTERNET_APP_MAX_LINKS);
    ble_scatternet_app_le_gap_init();
    ble_scatternet_app_le_profile_init();
    ble_scatternet_task_init();

    return 0;
}

#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
int ble_scatternet_config_at_cmd_config(void)
{
	T_GAP_CONN_INFO conn_info;
	T_GAP_DEV_STATE new_state;

	while(!(wifi_is_up(RTW_STA_INTERFACE) || wifi_is_up(RTW_AP_INTERFACE))) {
		vTaskDelay(1000 / portTICK_RATE_MS);
	}
	
	set_bt_config_state(BC_DEV_INIT); // BT Config on
	
#if CONFIG_AUTO_RECONNECT
	/* disable auto reconnect */
	wifi_set_autoreconnect(0);
#endif

	wifi_disconnect();

#if CONFIG_LWIP_LAYER
	LwIP_ReleaseIP(WLAN0_IDX);
#endif

	le_get_conn_info(bt_config_conn_id, &conn_info);
	bt_config_gap_conn_state = conn_info.conn_state;

	le_get_gap_param(GAP_PARAM_DEV_STATE , &new_state);
	if (new_state.gap_init_state == GAP_INIT_STATE_STACK_READY) {
		printf("[BLE Scatternet]BT Stack already on\n\r");
		airsync_specific = 0;
		bt_config_wifi_init();
		bt_config_app_set_adv_data();
		ble_scatternet_send_msg(1); //Start ADV
		set_bt_config_state(BC_DEV_IDLE); // BT Config Ready
		BC_printf("BT Config Wifi ready\n\r");
	}
	else{
		printf("[BLE Scatternet]BT Stack not ready \n\r");
	}
	return 0;
}
#endif

extern void wifi_btcoex_set_bt_on(void);
int ble_scatternet_app_init(void)
{
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
	int bt_stack_already_on = 0;
	T_GAP_CONN_INFO conn_info;
#endif
	//(void) bt_stack_already_on;
	T_GAP_DEV_STATE new_state;

	//uint32_t random_1 = 0;
	/*Wait WIFI init complete*/
	while(!(wifi_is_up(RTW_STA_INTERFACE) || wifi_is_up(RTW_AP_INTERFACE))) {
		vTaskDelay(1000 / portTICK_RATE_MS);
	}
	
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG) 
	set_bt_config_state(BC_DEV_INIT); // BT Config on

#if CONFIG_AUTO_RECONNECT
	/* disable auto reconnect */
	wifi_set_autoreconnect(0);
#endif

	wifi_disconnect();

#if CONFIG_LWIP_LAYER
	LwIP_ReleaseIP(WLAN0_IDX);
#endif

	le_get_conn_info(bt_config_conn_id, &conn_info);
	bt_config_gap_conn_state = conn_info.conn_state;
#endif

	//judge BLE central is already on
	le_get_gap_param(GAP_PARAM_DEV_STATE , &new_state);
	if (new_state.gap_init_state == GAP_INIT_STATE_STACK_READY) {
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
		bt_stack_already_on = 1;
#endif
		printf("[BLE Scatternet]BT Stack already on\n\r");
		return 0;
	}
	else
		ble_scatternet_app_main();
	bt_coex_init();

	/*Wait BT init complete*/
	do {
		vTaskDelay(100 / portTICK_RATE_MS);
		le_get_gap_param(GAP_PARAM_DEV_STATE , &new_state);
	}while(new_state.gap_init_state != GAP_INIT_STATE_STACK_READY);

	/*Start BT WIFI coexistence*/
	wifi_btcoex_set_bt_on();
	
#if defined (CONFIG_BT_CENTRAL_CONFIG) && (CONFIG_BT_CENTRAL_CONFIG)
	if (bt_stack_already_on) {
		bt_config_app_set_adv_data();
		ble_scatternet_send_msg(1); //Start ADV
		set_bt_config_state(BC_DEV_IDLE); // BT Config Ready
	}
#endif

	return 0;

}

extern void gcs_delete_client(void);
void ble_scatternet_app_deinit(void)
{
#if ((defined(CONFIG_BT_SCATTERNET) && CONFIG_BT_SCATTERNET) && \
	(defined(CONFIG_BT_CENTRAL_CONFIG) && CONFIG_BT_CENTRAL_CONFIG))
	set_bt_config_state(BC_DEV_DEINIT);
	bt_config_wifi_deinit();
	set_bt_config_state(BC_DEV_DISABLED);
#endif

	ble_scatternet_app_task_deinit();
	T_GAP_DEV_STATE state;
	le_get_gap_param(GAP_PARAM_DEV_STATE , &state);
	if (state.gap_init_state != GAP_INIT_STATE_STACK_READY) {
		printf("[BLE Scatternet]BT Stack is not running\n\r");
	}
#if F_BT_DEINIT
	else {
		gcs_delete_client();
		bte_deinit();
		bt_trace_uninit();
		printf("[BLE Scatternet]BT Stack deinitalized\n\r");
	}
#endif
}

/** @} */ /* End of group CENTRAL_CLIENT_DEMO_MAIN */


