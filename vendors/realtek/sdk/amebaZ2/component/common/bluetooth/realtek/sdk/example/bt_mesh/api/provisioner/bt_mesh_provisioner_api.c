/**
*****************************************************************************************
*     Copyright(c) 2019, Realtek Semiconductor Corporation. All rights reserved.
*****************************************************************************************
  * @file     bt_mesh_provisioner_api.c
  * @brief    Source file for provisioner cmd.
  * @details  User command interfaces.
  * @author   sherman
  * @date     2019-09-16
  * @version  v1.0
  * *************************************************************************************
  */
#include "bt_mesh_provisioner_api.h"

#if defined(CONFIG_BT_MESH_USER_API) && CONFIG_BT_MESH_USER_API
#if (defined(CONFIG_BT_MESH_PROVISIONER) && CONFIG_BT_MESH_PROVISIONER || \
    defined(CONFIG_BT_MESH_PROVISIONER_MULTIPLE_PROFILE) && CONFIG_BT_MESH_PROVISIONER_MULTIPLE_PROFILE)

struct task_struct	      meshProvisionerCmdThread;
extern CMD_MOD_INFO_S     btMeshCmdPriv;
extern INDICATION_ITEM    btMeshCmdIdPriv;

thread_return mesh_provisioner_cmd_thread(thread_context context)
{
	uint8_t count = 0;
    uint32_t time_out = 2000;
    uint16_t mesh_code = MAX_MESH_PROVISIONER_CMD;
    CMD_ITEM *pmeshCmdItem = NULL;
    CMD_ITEM_S *pmeshCmdItem_s = NULL;
    PUSER_ITEM puserItem = NULL;
    user_cmd_parse_value_t *pparse_value = NULL;
    struct list_head *plist;
	struct task_struct *pcmdtask = &(meshProvisionerCmdThread);    
    
    printf("[BT_MESH] %s(): mesh cmd thread enter !\r\n", __func__);
	rtw_up_sema(&pcmdtask->terminate_sema);
	while(1)
	{		
		if (rtw_down_sema(&pcmdtask->wakeup_sema) == _FAIL) {
            printf("[BT_MESH] %s(): down wakeup_sema fail !\r\n", __func__);
            break;
		}
		if (pcmdtask->blocked == _TRUE)
		{
			printf("[BT_MESH] %s(): blocked(%d) !\r\n", __func__, pcmdtask->blocked);
			break;
		}
        plist = bt_mesh_dequeue_cmd();
        if (!plist) {
            printf("[BT_MESH] %s(): bt_mesh_dequeue_cmd fail !\r\n", __func__);
			continue;
        }
        pmeshCmdItem_s = (CMD_ITEM_S *)plist;
        pmeshCmdItem = pmeshCmdItem_s->pmeshCmdItem;
        mesh_code = pmeshCmdItem->meshCmdCode;
        pparse_value = pmeshCmdItem->pparseValue;
        puserItem = (PUSER_ITEM)pmeshCmdItem->userData;
        if (pmeshCmdItem_s->semaDownTimeOut) {
            printf("[BT_MESH] %s(): mesh cmd %d has already timeout !\r\n", __func__, mesh_code);
            bt_mesh_cmdunreg(pmeshCmdItem_s);
            continue;
        }
        BT_MESH_CMD_ID_PRIV_MOD(mesh_code, pmeshCmdItem_s, puserItem->userApiMode, 0, plt_time_read_ms());
        if (pmeshCmdItem_s->pmeshCmdItem->meshFunc) {
            pmeshCmdItem_s->pmeshCmdItem->meshFunc(mesh_code, pmeshCmdItem_s);
            if (puserItem->userApiMode != USER_API_SYNCH) {
                BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
                continue;
            }
            /* proivsioning takes more time */
            if (mesh_code == GEN_MESH_CODE(_prov)) {
                time_out = 10000;
            } else {
                time_out = 2000;
            }
            /* guarantee the integrity of each mesh_code cmd */
            if (rtw_down_timeout_sema(&btMeshCmdPriv.meshThreadSema, time_out) == _FAIL) {
                count++;
                BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 1, 0);
                if (count == BT_MESH_PROV_CMD_RETRY_COUNT) {
                    count = 0;
                    printf("[BT_MESH] %s(): down sema timeout mesh cmd = %d !\r\n", __func__, mesh_code);
                    while (!pmeshCmdItem_s->msgRecvFlag) {
                        printf("[BT_MESH] %s(): Waite bt_mesh_user_cmd_hdl implementation !\r\n", __func__, mesh_code);
                        plt_delay_ms(100);
                    }
                    bt_mesh_cmdunreg(pmeshCmdItem_s);
                } else {
                    /* enqueue to head for another try */
                    if (bt_mesh_enqueue_cmd(plist, 1) == 1) {
                        printf("[BT_MESH] %s(): enqueue cmd for mesh code %d fail !\r\n", __func__, mesh_code);
                        bt_mesh_cmdunreg(pmeshCmdItem_s);
                    }
                }
            }else {
                count = 0;
                printf("[BT_MESH] %s():mesh cmd = %d puserItem->userCmdResult = %d !\r\n", __func__, mesh_code, puserItem->userCmdResult);
                bt_mesh_cmdunreg(pmeshCmdItem_s);
                BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
            } 
            
        } else {
            printf("[BT_MESH] %s(): meshFunc is NULL mesh_code = %d !\r\n", __func__, mesh_code);
            bt_mesh_cmdunreg(pmeshCmdItem_s);
            BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
        }      
	}
    /* prevent previous cmd involking bt_mesh_indication */
    BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
	/* free all pmeshCmdItem_s resources */
	do{
		plist = bt_mesh_dequeue_cmd();
		if (plist == NULL) {
            break;
        }
		bt_mesh_cmdunreg((CMD_ITEM_S *)plist);
	}while(1);	
	rtw_up_sema(&pcmdtask->terminate_sema);
    printf("[BT_MESH] %s(): mesh cmd thread exit !\r\n", __func__);
    
	rtw_thread_exit();
}

uint8_t bt_mesh_provisioner_api_init(void)
{
    if (btMeshCmdPriv.meshMode == BT_MESH_PROVISIONER) {
        printf("[BT_MESH] %s(): MESH PROVISIONER is already running !\r\n", __func__);
        return 1;
    } else if (btMeshCmdPriv.meshMode == BT_MESH_DEVICE) {
        printf("[BT_MESH] %s(): MESH Device is running, need deinitialization firstly !\r\n", __func__);
        return 1;
    }
    rtw_init_listhead(&btMeshCmdPriv.meshCmdList);
    rtw_mutex_init(&btMeshCmdPriv.cmdMutex);
    rtw_init_sema(&btMeshCmdPriv.meshThreadSema, 0);
    BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
    if (rtw_if_wifi_create_task(&meshProvisionerCmdThread, "mesh_provisioner_cmd_thread", 1024, TASK_PRORITY_MIDDLE, mesh_provisioner_cmd_thread, NULL)!= 1) {
        printf("[BT_MESH] %s(): create mesh_provisioner_cmd_thread fail !\r\n", __func__);
        rtw_mutex_free(&btMeshCmdPriv.cmdMutex);
        rtw_free_sema(&btMeshCmdPriv.meshThreadSema);
        return 1;
    }
    btMeshCmdPriv.meshMode = BT_MESH_PROVISIONER;
    btMeshCmdPriv.meshCmdEnable = 1;

    return 0;
}

void bt_mesh_provisioner_api_deinit(void)
{
    if (btMeshCmdPriv.meshMode != BT_MESH_PROVISIONER) {
        printf("[BT_MESH] %s(): MESH PROVISIONER is not running !\r\n", __func__);
        return;
    }
    btMeshCmdPriv.meshMode = 0;
    btMeshCmdPriv.meshCmdEnable = 0;
    BT_MESH_CMD_ID_PRIV_MOD(MAX_MESH_PROVISIONER_CMD, NULL, USER_API_DEFAULT_MODE, 0, 0);
    rtw_if_wifi_delete_task(&meshProvisionerCmdThread);
    rtw_mutex_free(&btMeshCmdPriv.cmdMutex);
    rtw_free_sema(&btMeshCmdPriv.meshThreadSema);
}

#endif
#endif
