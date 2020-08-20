#include "bt_mesh_app_list_intf.h"
#include "bt_mesh_app_lib_intf.h"
#include "mesh_api.h"

#define BT_MESH_APP_LIST_INTF_DEBUG		1
#define LIST_FLUSHING_TIMEOUT			5
#define BT_MESH_EVENT_HEADER_LENGTH		4

MESH_FLUSH_INFO_S            btMeshFlushPriv;
struct task_struct	         meshFlushingThread;
struct BT_MESH_PRIV          bt_mesh_priv;
uint8_t                      stop_list_flushing_flag = 0;

void list_flushing_handler(_list *plist, uint8_t mesh_flush_event)
{
    MESH_FLUSH_ITEM_S  *pmeshFlushItem_s = NULL;

    pmeshFlushItem_s = (MESH_FLUSH_ITEM_S *)rtw_zmalloc(sizeof(MESH_FLUSH_ITEM_S));
    if (pmeshFlushItem_s == NULL) {
        printf("[BT_MESH_DEMO] %s(): alloc pmeshFlushItem_s fail !\r\n", __func__);
        return;
    }
    pmeshFlushItem_s->plist = plist;
    pmeshFlushItem_s->meshFlushEvent = mesh_flush_event;
    bt_mesh_enqueue_flush_event(&pmeshFlushItem_s->list);

    return;
}

void list_flushing_timer_handler(void *FunctionContext)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint32_t current_time = rtw_get_current_time();
    uint8_t flag = 0;

	if (stop_list_flushing_flag) {
#if 0 //BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] APP is processing, stop list flushing to maintain the topology!\r\n");
#endif
		rtw_set_timer(&bt_mesh_priv.list_flushing_timer, LIST_FLUSHING_TIMEOUT * 1000);
		return;
	}

#if 0 //BT_MESH_APP_LIST_INTF_DEBUG
	printf("[BT_MESH_DEMO] list_flushing_timer timeout!\r\n");
#endif

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.unproed_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.unproed_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (current_time > pbt_mesh_device_info->last_scan_time && rtw_systime_to_sec(current_time - pbt_mesh_device_info->last_scan_time) > 120) {
			rtw_list_delete(plist);
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] UUID = 0x");
			for(uint8_t i = 0; i < 16; i++)
				printf("%02X", pbt_mesh_device_info->dev_uuid[i]);
			printf(" remove from unproed_dev_list & free in %s()!\r\n", __FUNCTION__);
#endif
			rtw_mfree((uint8_t *)pbt_mesh_device_info, sizeof(struct BT_MESH_DEVICE_INFO));
		}

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (current_time > pbt_mesh_device_info->last_active_time && rtw_systime_to_sec(current_time - pbt_mesh_device_info->last_active_time) > 120) {
            /* enqueue list to bt_mesh_flushing_thread for double check caused btc leak */
            if (pbt_mesh_device_info->get_group_addr_flag && pbt_mesh_device_info->active_state) {
                list_flushing_handler(plist, COMBO_CHECK);
    		} else {
                list_flushing_handler(plist, CONNECTION_CHECK_EVENT);
            }
            flag = 1;
		}
        if (!flag) {
            if (pbt_mesh_device_info->get_group_addr_flag && pbt_mesh_device_info->active_state) {
                list_flushing_handler(plist, GET_NODE_GROUP);
    		}
        }
		flag = 0;
		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	rtw_set_timer(&bt_mesh_priv.list_flushing_timer, LIST_FLUSHING_TIMEOUT * 1000);
}

void init_bt_mesh_priv(void)
{
	rtw_memset(&bt_mesh_priv, 0, sizeof(bt_mesh_priv));

	rtw_init_listhead(&bt_mesh_priv.unproed_dev_list);
	rtw_init_listhead(&bt_mesh_priv.proed_active_dev_list);
	rtw_init_listhead(&bt_mesh_priv.proed_inactive_dev_list);
	rtw_init_listhead(&bt_mesh_priv.processing_dev_list);

	rtw_spinlock_init(&bt_mesh_priv.dev_list_lock);

	rtw_init_timer(&bt_mesh_priv.list_flushing_timer, NULL, list_flushing_timer_handler, NULL, "list_flushing_timer");
	rtw_set_timer(&bt_mesh_priv.list_flushing_timer, LIST_FLUSHING_TIMEOUT * 1000);

	bt_mesh_priv.provisioner_started_flag = _FALSE;
}

void add_unproed_dev(uint8_t *dev_uuid)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint32_t current_time = rtw_get_current_time();
	uint8_t flag = 0;

	if (stop_list_flushing_flag) {
#if 0 //BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] APP is processing, stop add unproed dev to maintain the topology!\r\n");
#endif
		return;
	}

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.unproed_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.unproed_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (rtw_memcmp(pbt_mesh_device_info->dev_uuid, dev_uuid, 16) == 1) {
			flag = 1;
			break;
		}

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	if (flag) {
		pbt_mesh_device_info->last_scan_time = current_time;
#if 0 //BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] UUID = 0x");
		for (uint8_t i = 0; i < 16; i++)
			printf("%02X", dev_uuid[i]);
		printf(" update last_scan_time in %s()!\r\n", __FUNCTION__);
#endif
	} else {
		pbt_mesh_device_info = (struct BT_MESH_DEVICE_INFO *)rtw_zmalloc(sizeof(struct BT_MESH_DEVICE_INFO));
		if (pbt_mesh_device_info == NULL) {
			printf("[BT_MESH_DEMO] Malloc pbt_mesh_device_info fail!\r\n");
			return;
		}

		rtw_memcpy(pbt_mesh_device_info->dev_uuid, dev_uuid, 16);
		pbt_mesh_device_info->last_scan_time = current_time;
#if BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] UUID = 0x");
		for (uint8_t j = 0; j < 16; j++)
			printf("%02X", dev_uuid[j]);
		printf(" malloc & add to unproed_dev_list in %s()!\r\n", __FUNCTION__);
#endif
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.unproed_dev_list);
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	}
}

void update_proed_dev(uint16_t mesh_addr, uint8_t light_state)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint32_t current_time = rtw_get_current_time();
	uint8_t active_flag = 0, inactive_flag = 0;

	if (stop_list_flushing_flag) {
#if 0 //BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] APP is processing, stop update proed dev to maintain the topology!\r\n");
#endif
		return;
	}

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
			active_flag = 1;
			break;
		}

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	if (active_flag) {
		pbt_mesh_device_info->last_active_time = current_time;
		pbt_mesh_device_info->light_state = light_state;
#if BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] Mesh Addr = 0x%x update last_active_time in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
	} else {
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		plist = get_next(&bt_mesh_priv.proed_inactive_dev_list);
		while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_inactive_dev_list, plist)) == _FALSE) {
			plist_next = get_next(plist);
			pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

			if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
				inactive_flag = 1;
				rtw_list_delete(plist);
				pbt_mesh_device_info->last_active_time = current_time;
				pbt_mesh_device_info->light_state = light_state;
				pbt_mesh_device_info->active_state = 1;
#if BT_MESH_APP_LIST_INTF_DEBUG
				printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_inactive_dev_list to proed_active_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
				rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);
				break;
			}

			plist = plist_next;
		}
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

		if (!inactive_flag) {
			pbt_mesh_device_info = (struct BT_MESH_DEVICE_INFO *)rtw_zmalloc(sizeof(struct BT_MESH_DEVICE_INFO));
			if (pbt_mesh_device_info == NULL) {
				printf("[BT_MESH_DEMO] Malloc pbt_mesh_device_info fail!\r\n");
				return;
			}

			pbt_mesh_device_info->mesh_addr = mesh_addr;
			pbt_mesh_device_info->last_active_time = current_time;
			pbt_mesh_device_info->light_state = light_state;
			pbt_mesh_device_info->active_state = 1;
			pbt_mesh_device_info->get_group_addr_flag = 1;
			list_flushing_handler(plist, GET_NODE_GROUP);
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x malloc & add to proed_active_dev_list in %s()!\r\n", mesh_addr, __FUNCTION__);
#endif
			rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);
			rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		}
	}
}

void update_node_group(uint16_t mesh_addr, uint16_t group_addr)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t flag = 0;
	
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
			flag = 1;
			break;
		}

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	if (flag) {
		pbt_mesh_device_info->group_addr = group_addr;
		pbt_mesh_device_info->get_group_addr_flag = 0;
#if BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] Mesh Addr = 0x%x update Group Addr = 0x%x in %s()!\r\n", pbt_mesh_device_info->mesh_addr, pbt_mesh_device_info->group_addr, __FUNCTION__);
#endif
	}
}

/* RTW_BT_CMD_START_PROVISIONER */
void bt_mesh_cmd_start_provisioner_handler(uint8_t *bt_mesh_event_value)
{
	uint8_t ret;

	if (bt_mesh_priv.provisioner_started_flag == _TRUE) {
		*(bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH) = 2;
		return;
	}

	ret = bt_mesh_cmd_start_provisioner_api();
	if (ret == 1)
		bt_mesh_priv.provisioner_started_flag = _TRUE;

	*(bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH) = ret;
}

/* RTW_BT_CMD_GET_UNPROVISIONED_DEVICE */
void bt_mesh_cmd_get_unprovisioned_device_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.unproed_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.unproed_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		rtw_memcpy(bt_mesh_event_value, pbt_mesh_device_info->dev_uuid, 16);
		bt_mesh_event_value += 16;

		num ++;

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

/* RTW_BT_CMD_CONNECT_DEVICE */
void bt_mesh_cmd_connect_device_pre_handler(uint8_t *dev_uuid)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.unproed_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.unproed_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (rtw_memcmp(pbt_mesh_device_info->dev_uuid, dev_uuid, 16) == 1) {
			rtw_list_delete(plist);
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] UUID = 0x");
			for (uint8_t i = 0; i < 16; i++)
				printf("%02X", dev_uuid[i]);
			printf(" move from unproed_dev_list to processing_dev_list in %s()!\r\n", __FUNCTION__);
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.processing_dev_list);
			break;
		}

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
}

void bt_mesh_cmd_connect_device_handler(void)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint16_t mesh_addr;
	uint8_t light_state;
	uint32_t current_time = rtw_get_current_time();

	plist = &bt_mesh_priv.processing_dev_list;
	while (1) {
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		plist = get_next(plist);
		if ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _TRUE) {
			rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
			break;
		}
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

		pbt_mesh_device_info->connect_status = bt_mesh_cmd_connect_device_api(pbt_mesh_device_info->dev_uuid, &mesh_addr, &light_state);
		if (pbt_mesh_device_info->connect_status == 1) {
			pbt_mesh_device_info->mesh_addr = mesh_addr;
			pbt_mesh_device_info->last_active_time = current_time;
			pbt_mesh_device_info->light_state = light_state;
			pbt_mesh_device_info->active_state = 1;
		}
	}
}

void bt_mesh_cmd_connect_device_post_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.processing_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		rtw_memcpy(bt_mesh_event_value, pbt_mesh_device_info->dev_uuid, 16);
		bt_mesh_event_value += 16;

		*bt_mesh_event_value = pbt_mesh_device_info->connect_status;
		bt_mesh_event_value += 1;

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->light_state;
		bt_mesh_event_value += 1;

		rtw_list_delete(plist);
		if (pbt_mesh_device_info->connect_status == 1) {
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] UUID = 0x");
			for (uint8_t i = 0; i < 16; i++)
				printf("%02X", pbt_mesh_device_info->dev_uuid[i]);
			printf(" move from processing_dev_list to proed_active_dev_list in %s()!\r\n", __FUNCTION__);
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);
		} else {
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] UUID = 0x");
			for (uint8_t j = 0; j < 16; j++)
				printf("%02X", pbt_mesh_device_info->dev_uuid[j]);
			printf(" move from processing_dev_list to unproed_dev_list in %s()!\r\n", __FUNCTION__);
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.unproed_dev_list);
		}

		num ++;

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

/* RTW_BT_CMD_DELETE_NODE */
void bt_mesh_cmd_delete_node_pre_handler(uint16_t mesh_addr)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
			rtw_list_delete(plist);
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to processing_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);			
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.processing_dev_list);
			break;
		}

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
}

void bt_mesh_cmd_delete_node_handler(void)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	plist = &bt_mesh_priv.processing_dev_list;
	while (1) {
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		plist = get_next(plist);
		if ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _TRUE) {
			rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
			break;
		}
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

		pbt_mesh_device_info->delete_status = bt_mesh_cmd_delete_node_api(pbt_mesh_device_info->mesh_addr);
	}
}

void bt_mesh_cmd_delete_node_post_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.processing_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->delete_status;
		bt_mesh_event_value += 1;

		rtw_list_delete(plist);
		if (pbt_mesh_device_info->delete_status == 1) {
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x remove from processing_dev_list & free in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
			rtw_mfree((uint8_t *)pbt_mesh_device_info, sizeof(struct BT_MESH_DEVICE_INFO));
		} else {
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from processing_dev_list to proed_active_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);
		}

		num ++;

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

/* RTW_BT_CMD_GET_NODE_STATUS */
void bt_mesh_cmd_get_node_status_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->group_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->active_state;
		bt_mesh_event_value += 1;

		*bt_mesh_event_value = pbt_mesh_device_info->light_state;
		bt_mesh_event_value += 1;

		num ++;

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_inactive_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_inactive_dev_list, plist)) == _FALSE) {
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->group_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->active_state;
		bt_mesh_event_value += 1;

		*bt_mesh_event_value = pbt_mesh_device_info->light_state;
		bt_mesh_event_value += 1;

		num ++;

		plist = get_next(plist);
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

/* RTW_BT_CMD_SET_NODE_STATE */
void bt_mesh_cmd_set_node_state_pre_handler(uint16_t mesh_addr, uint8_t light_state)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (MESH_IS_UNICAST_ADDR(mesh_addr)) {
			if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
				rtw_list_delete(plist);
				pbt_mesh_device_info->target_light_state = light_state;
				pbt_mesh_device_info->set_node_state_flag = 0;
#if BT_MESH_APP_LIST_INTF_DEBUG
				printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to processing_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__); 		
#endif
				rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.processing_dev_list);

				break;
			}
		} else if (MESH_IS_GROUP_ADDR(mesh_addr)) {
			if (pbt_mesh_device_info->group_addr == mesh_addr) {
				rtw_list_delete(plist);
				pbt_mesh_device_info->target_light_state = light_state;
				pbt_mesh_device_info->set_node_state_flag = 1;
#if BT_MESH_APP_LIST_INTF_DEBUG
				printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to processing_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__); 		
#endif
				rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.processing_dev_list);

			}
		}

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
}

void bt_mesh_cmd_set_node_state_handler(void)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t light_state;

	plist = &bt_mesh_priv.processing_dev_list;
	while (1) {
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		plist = get_next(plist);
		if ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _TRUE) {
			rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
			break;
		}
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

		light_state = bt_mesh_cmd_set_node_state_api(pbt_mesh_device_info->mesh_addr, pbt_mesh_device_info->group_addr, pbt_mesh_device_info->target_light_state, pbt_mesh_device_info->set_node_state_flag);
		if (light_state != 0xFF) {
			pbt_mesh_device_info->light_state = light_state;
		}
	}
}

void bt_mesh_cmd_set_node_state_post_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.processing_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->group_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->active_state;
		bt_mesh_event_value += 1;

		*bt_mesh_event_value = pbt_mesh_device_info->light_state;
		bt_mesh_event_value += 1;

		rtw_list_delete(plist);
#if BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from processing_dev_list to proed_active_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
		rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);

		num ++;

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

/* RTW_BT_CMD_SET_NODE_GROUP */
void bt_mesh_cmd_set_node_group_pre_handler(uint16_t mesh_addr, uint16_t group_addr)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		if (pbt_mesh_device_info->mesh_addr == mesh_addr) {
			rtw_list_delete(plist);
			pbt_mesh_device_info->target_group_addr = group_addr;
#if BT_MESH_APP_LIST_INTF_DEBUG
			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to processing_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__); 		
#endif
			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.processing_dev_list);
			break;
		}

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
}

void bt_mesh_cmd_set_node_group_handler(void)
{
	_list *plist;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;

	plist = &bt_mesh_priv.processing_dev_list;
	while (1) {
		rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
		plist = get_next(plist);
		if ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _TRUE) {
			rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
			break;
		}
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);
		rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

		if (bt_mesh_cmd_set_node_group_api(pbt_mesh_device_info->mesh_addr, pbt_mesh_device_info->target_group_addr)) {
			pbt_mesh_device_info->group_addr = pbt_mesh_device_info->target_group_addr;
		}
	}
}

void bt_mesh_cmd_set_node_group_post_handler(uint8_t *bt_mesh_event_value)
{
	_list *plist, *plist_next;
	struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
	uint8_t num = 0;
	uint8_t *pnum = bt_mesh_event_value + BT_MESH_EVENT_HEADER_LENGTH;

	bt_mesh_event_value += BT_MESH_EVENT_HEADER_LENGTH + 1;
	rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
	plist = get_next(&bt_mesh_priv.processing_dev_list);
	while ((rtw_end_of_queue_search(&bt_mesh_priv.processing_dev_list, plist)) == _FALSE) {
		plist_next = get_next(plist);
		pbt_mesh_device_info = LIST_CONTAINOR(plist, struct BT_MESH_DEVICE_INFO, list);

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->mesh_addr;
		bt_mesh_event_value += 2;

		*((uint16_t *)bt_mesh_event_value) = pbt_mesh_device_info->group_addr;
		bt_mesh_event_value += 2;

		*bt_mesh_event_value = pbt_mesh_device_info->active_state;
		bt_mesh_event_value += 1;

		*bt_mesh_event_value = pbt_mesh_device_info->light_state;
		bt_mesh_event_value += 1;

		rtw_list_delete(plist);
#if BT_MESH_APP_LIST_INTF_DEBUG
		printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from processing_dev_list to proed_active_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
		rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_active_dev_list);

		num ++;

		plist = plist_next;
	}
	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);

	*pnum = num;
	bt_mesh_event_value = pnum - BT_MESH_EVENT_HEADER_LENGTH;
}

thread_return bt_mesh_flushing_thread(thread_context context)
{
    MESH_FLUSH_ITEM_S          *pmeshFlushItem_s = NULL;
    struct list_head           *plist, *plist_next;
	struct task_struct         *pcmdtask = &(meshFlushingThread);
    struct BT_MESH_DEVICE_INFO *pbt_mesh_device_info;
    uint8_t                    light_state;
    
    printf("[BT_MESH_DEMO] %s(): mesh flushing thread enter !\r\n", __func__);
	rtw_up_sema(&pcmdtask->terminate_sema);
	while(1)
	{		
		if (rtw_down_sema(&pcmdtask->wakeup_sema) == _FAIL) {
            printf("[BT_MESH_DEMO] %s(): down wakeup_sema fail !\r\n", __func__);
            break;
		}
		if (pcmdtask->blocked == _TRUE)
		{
			printf("[BT_MESH_DEMO] %s(): blocked(%d) !\r\n", __func__, pcmdtask->blocked);
			break;
		}
        pmeshFlushItem_s = (MESH_FLUSH_ITEM_S *)bt_mesh_dequeue_flush_event();
        if (!pmeshFlushItem_s) {
            printf("[BT_MESH_DEMO] %s(): bt_mesh_dequeue_flush_event fail !\r\n", __func__);
			continue;
        }
        switch (pmeshFlushItem_s->meshFlushEvent) {
            case CONNECTION_CHECK_EVENT:
            {
                pbt_mesh_device_info = LIST_CONTAINOR(pmeshFlushItem_s->plist, struct BT_MESH_DEVICE_INFO, list);
                light_state = bt_mesh_cmd_get_node_state_api(pbt_mesh_device_info->mesh_addr);
                /* node really inactive */
                if (light_state == 0xFF) {
                    rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
                	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
                	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
                		plist_next = get_next(plist);
                		if (plist == pmeshFlushItem_s->plist) {
                            rtw_list_delete(plist);
                			pbt_mesh_device_info->active_state = 0;
#if BT_MESH_APP_LIST_INTF_DEBUG
                			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to proed_inactive_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
                			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_inactive_dev_list);
                            break;
                        }
                        plist = plist_next;
                	}
                	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
        		}
            }
            break;

            case COMBO_CHECK:
            {
                pbt_mesh_device_info = LIST_CONTAINOR(pmeshFlushItem_s->plist, struct BT_MESH_DEVICE_INFO, list);
                light_state = bt_mesh_cmd_get_node_state_api(pbt_mesh_device_info->mesh_addr);
                /* node really inactive */
                if (light_state == 0xFF) {
                    rtw_enter_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
                	plist = get_next(&bt_mesh_priv.proed_active_dev_list);
                	while ((rtw_end_of_queue_search(&bt_mesh_priv.proed_active_dev_list, plist)) == _FALSE) {
                		plist_next = get_next(plist);
                		if (plist == pmeshFlushItem_s->plist) {
                            rtw_list_delete(plist);
                			pbt_mesh_device_info->active_state = 0;
#if BT_MESH_APP_LIST_INTF_DEBUG
                			printf("[BT_MESH_DEMO] Mesh Addr = 0x%x move from proed_active_dev_list to proed_inactive_dev_list in %s()!\r\n", pbt_mesh_device_info->mesh_addr, __FUNCTION__);
#endif
                			rtw_list_insert_tail(&pbt_mesh_device_info->list, &bt_mesh_priv.proed_inactive_dev_list);
                            break;
                        }
                        plist = plist_next;
                	}
                	rtw_exit_critical_bh(&bt_mesh_priv.dev_list_lock, NULL);
        		}
                bt_mesh_cmd_get_node_group_api(pbt_mesh_device_info->mesh_addr);
            }
            break; 

            case GET_NODE_GROUP:
            {
                pbt_mesh_device_info = LIST_CONTAINOR(pmeshFlushItem_s->plist, struct BT_MESH_DEVICE_INFO, list);
                bt_mesh_cmd_get_node_group_api(pbt_mesh_device_info->mesh_addr);
            }
            break;
            
            default:
            {
                printf("[BT_MESH_DEMO] %s(): meshFlushEvent %d not found !\r\n", __func__, pmeshFlushItem_s->meshFlushEvent);
            }
            break;        
        } 
        rtw_mfree((uint8_t *)pmeshFlushItem_s, sizeof(MESH_FLUSH_ITEM_S));
	}
	/* free all pmeshCmdItem_s resources */
	do{
		plist = bt_mesh_dequeue_flush_event();
		if (plist == NULL) {
            break;
        }
		rtw_mfree((uint8_t *)plist, sizeof(MESH_FLUSH_ITEM_S));
	}while(1);	
	rtw_up_sema(&pcmdtask->terminate_sema);
    printf("[BT_MESH_DEMO] %s(): mesh_demo_flushing_thread exit !\r\n", __func__);
    
	rtw_thread_exit();
}

uint8_t bt_mesh_demo_start_drv_thread(void)
{
    rtw_init_listhead(&btMeshFlushPriv.meshFlushList);
    rtw_mutex_init(&btMeshFlushPriv.eventMutex);
    if (rtw_if_wifi_create_task(&meshFlushingThread, "mesh_demo_flushing_thread", 1024, TASK_PRORITY_MIDDLE, bt_mesh_flushing_thread, NULL)!= 1) {
        printf("[BT_MESH_DEMO] %s(): create bt_mesh_flushing_thread fail !\r\n", __func__);
        rtw_mutex_free(&btMeshFlushPriv.eventMutex);
        return 1;
    }

    return 0;
}

void bt_mesh_demo_stop_drv_thread(void)
{
    rtw_if_wifi_delete_task(&meshFlushingThread);
    rtw_mutex_free(&btMeshFlushPriv.eventMutex);
}

uint8_t bt_mesh_enqueue_flush_event(struct list_head *queue)
{
    rtw_mutex_get(&btMeshFlushPriv.eventMutex);
    rtw_list_insert_tail(queue, &btMeshFlushPriv.meshFlushList);
    btMeshFlushPriv.eventListNum ++;
    rtw_mutex_put(&btMeshFlushPriv.eventMutex);
    rtw_if_wifi_wakeup_task(&meshFlushingThread);
    
    return 0;
}

struct list_head *bt_mesh_dequeue_flush_event(void)
{
    struct list_head *queue = NULL;
    
    rtw_mutex_get(&btMeshFlushPriv.eventMutex);
    if (rtw_is_list_empty(&btMeshFlushPriv.meshFlushList)) {
        printf("[BT_MESH_DEMO] %s(): btMeshFlushPriv.meshFlushList is empty !\r\n", __func__);
        rtw_mutex_put(&btMeshFlushPriv.eventMutex);
        return NULL;
    } else {
        queue = get_next(&btMeshFlushPriv.meshFlushList);
        rtw_list_delete(queue);
    }
    btMeshFlushPriv.eventListNum --;
    rtw_mutex_put(&btMeshFlushPriv.eventMutex); 

    return queue;
}

