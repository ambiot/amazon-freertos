#include <gap.h>
#include <bt_types.h>
#include <string.h>
#include <trace_app.h>
#include "vendor_cmd.h"
#include "vendor_cmd_bt.h"
#include <gap_conn_le.h> 

P_FUN_GAP_APP_CB ext_app_cb = NULL;

#if BT_VENDOR_CMD_ONE_SHOR_SUPPORT
T_GAP_CAUSE le_vendor_one_shot_adv(void)
{
    uint8_t len = 1;
    uint8_t param[1];
    param[0] = HCI_EXT_SUB_ONE_SHOT_ADV;

    if (gap_vendor_cmd_req(HCI_LE_VENDOR_EXTENSION_FEATURE2, len, param) == GAP_CAUSE_SUCCESS)
    {
        return GAP_CAUSE_SUCCESS;
    }
    return GAP_CAUSE_SEND_REQ_FAILED;
}
#endif

/**
 * @brief Callback for gap common module to notify app
 * @param[in] cb_type callback msy type @ref GAP_COMMON_MSG_TYPE.
 * @param[in] p_cb_data point to callback data @ref T_GAP_CB_DATA.
 * @retval void
 * example: 
 * uint8_t data[] ={0x33 , 0x01, 0x02,0x03, 0x04, 0x05, 0x06, 0x07};
 * mailbox_to_bt(data, sizeof(data));
 * mailbox_to_bt_set_profile_report(NULL, 0);
 */
void app_gap_vendor_callback(uint8_t cb_type, void *p_cb_data)
{
    if (ext_app_cb)
    {
        ext_app_cb(cb_type, p_cb_data);
    }
    return;
}

void vendor_cmd_init(P_FUN_GAP_APP_CB app_cb)
{
    if(app_cb != NULL)
    {
        ext_app_cb = app_cb;
    }
    gap_register_vendor_cb(app_gap_vendor_callback);
}

