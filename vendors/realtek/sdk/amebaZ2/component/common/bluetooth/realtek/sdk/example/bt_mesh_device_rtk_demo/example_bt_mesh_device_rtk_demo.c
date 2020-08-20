#include "platform_opts.h"
#include "FreeRTOS.h"
#include "task.h"
#include <platform/platform_stdlib.h>
#include <wifi_conf.h>
#include <osdep_service.h>
#if defined(CONFIG_PLATFORM_8721D)
#include <platform_opts_bt.h>
#endif
#include "bt_config_wifi.h"

#include <gap_conn_le.h> 

#if defined(CONFIG_BT_MESH_DEVICE_RTK_DEMO) && CONFIG_BT_MESH_DEVICE_RTK_DEMO

//media
extern void amebacam_broadcast_demo_thread();
//httpd
extern void httpd_demo_init_thread();

void bt_mesh_example_init_thread(void *param)
{
	T_GAP_DEV_STATE new_state;

	/*Wait WIFI init complete*/
	while(!(wifi_is_up(RTW_STA_INTERFACE) || wifi_is_up(RTW_AP_INTERFACE))) {
		vTaskDelay(1000 / portTICK_RATE_MS);
	}
#if defined(CONFIG_BT_MESH_DEVICE) && CONFIG_BT_MESH_DEVICE
	/*Init BT mesh device*/
	bt_mesh_device_app_main();
#elif defined(CONFIG_BT_MESH_DEVICE_MULTIPLE_PROFILE) && CONFIG_BT_MESH_DEVICE_MULTIPLE_PROFILE
    bt_mesh_device_multiple_profile_app_main();
#endif

	bt_coex_init();

	/*Wait BT init complete*/
	do {
		vTaskDelay(100 / portTICK_RATE_MS);
		le_get_gap_param(GAP_PARAM_DEV_STATE , &new_state);
	} while (new_state.gap_init_state != GAP_INIT_STATE_STACK_READY);

	/*Start BT WIFI coexistence*/
	wifi_btcoex_set_bt_on();

    if (bt_mesh_device_api_init()) {
        printf("[BT Mesh Device] bt_mesh_device_api_init fail ! \n\r");
    }

	vTaskDelete(NULL);
}

void example_bt_mesh(void)
{
	//init bt config/bt mesh
	if(xTaskCreate(bt_mesh_example_init_thread, ((const char*)"bt_mesh_example_demo_init_thread"), 1024, NULL, tskIDLE_PRIORITY + 5 + PRIORITIE_OFFSET, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(bt_mesh_example_demo_init_thread) failed", __FUNCTION__);
}

#endif /* CONFIG_EXAMPLE_BT_MESH_DEMO */


