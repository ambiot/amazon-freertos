/**
 * Copyright (c) 2017, Realsil Semiconductor Corporation. All rights reserved.
 *
 */

#include <stdio.h>
#include <string.h>


#include "os_mem.h"
#include "hci_uart.h"
#include "trace_app.h"
#include "bt_board.h"
#include "os_sync.h"

#include "serial_api.h"
#include "serial_ex_api.h"

static serial_t hci_serial_obj;

#define HCI_UART_RX_BUF_SIZE        0x2000   /* RX buffer size 8K */
#define HCI_UART_RX_ENABLE_COUNT    (HCI_UART_RX_BUF_SIZE - 2 * (1021 + 5))   /* Enable RX */
#define HCI_UART_RX_DISABLE_COUNT   (HCI_UART_RX_BUF_SIZE - 1021 - 5 - 10)   /* Disable RX */
typedef struct
{
    //tx
    uint32_t            tx_len;
    P_UART_TX_CB        tx_cb; 
//rx
    bool                rx_disabled;
    uint16_t            rx_read_idx;
    uint16_t            rx_write_idx;
    uint8_t             rx_buffer[HCI_UART_RX_BUF_SIZE];
    void*               rx_timer_handle;
    P_UART_RX_CB        rx_ind;
    
    bool                hci_uart_bridge_flag;
}T_HCI_UART;

T_HCI_UART *hci_uart_obj;
//=========================HCI_UART_OUT=========================
void set_hci_uart_out(bool flag)
{
    T_HCI_UART *p_uart_obj = hci_uart_obj;
    if(p_uart_obj != NULL)
    {
       p_uart_obj->hci_uart_bridge_flag = flag;
    }
    else
    {
       hci_board_debug("set_hci_uart_out: hci_uart_obj is NULL");
    }
}

bool hci_uart_tx_bridge(uint8_t rc)
{
      serial_putc(&hci_serial_obj, rc);
      return true;
}

bool hci_uart_rx_bridge(uint8_t rc)
{
extern void bt_uart_tx(uint8_t rc);
      bt_uart_tx(rc);
      return true;
}

//=============================interal=========================
static void uart_send_done(uint32_t id)
{
    T_HCI_UART *hci_rtk_obj = hci_uart_obj;
    if (hci_rtk_obj->tx_cb)
    {
        hci_rtk_obj->tx_cb();
    }
}

void hci_uart_rx_disable(T_HCI_UART *hci_adapter)
{
    /* We disable received data available and rx timeout interrupt, then
     * the rx data will stay in UART FIFO, and RTS will be pulled high if
     * the watermark is higher than rx trigger level. */
    HCI_PRINT_INFO0("hci_uart_rx_disable");
    serial_rts_control(&hci_serial_obj,0); 
    hci_adapter->rx_disabled = true;
}

void hci_uart_rx_enable(T_HCI_UART *hci_adapter)
{
    HCI_PRINT_INFO0("comuart_rx_enable");
    serial_rts_control(&hci_serial_obj,1); 
    hci_adapter->rx_disabled = false;
}

uint8_t hci_rx_empty(void)
{
    uint16_t tmpRead = hci_uart_obj->rx_read_idx;
    uint16_t tmpWrite = hci_uart_obj->rx_write_idx;
    return (tmpRead == tmpWrite);
}

uint16_t hci_rx_data_len(void)
{
    return (hci_uart_obj->rx_write_idx + HCI_UART_RX_BUF_SIZE - hci_uart_obj->rx_read_idx) % HCI_UART_RX_BUF_SIZE;
}
uint16_t hci_rx_space_len(void)
{
    return (hci_uart_obj->rx_read_idx + HCI_UART_RX_BUF_SIZE - hci_uart_obj->rx_write_idx - 1) % HCI_UART_RX_BUF_SIZE;
}
static inline void uart_insert_char(T_HCI_UART *hci_adapter, uint8_t ch)
{
    /* Should neve happen */
    if (hci_rx_space_len() == 0)
    {
        HCI_PRINT_ERROR0("uart_insert_char: rx buffer full");
        hci_board_debug("uart_insert_char: rx buffer full");
        return;
    }
	
    if (rltk_wlan_is_mp() ) { //#if HCI_MP_BRIDGE
        if(hci_adapter->hci_uart_bridge_flag == true)
        {
          hci_uart_rx_bridge(ch);
          return;
        }
    }
	
    hci_adapter->rx_buffer[hci_adapter->rx_write_idx++] = ch;
    hci_adapter->rx_write_idx %= HCI_UART_RX_BUF_SIZE;

    if (hci_rx_data_len() >= HCI_UART_RX_DISABLE_COUNT && hci_adapter->rx_disabled == false)
    {
        hci_board_debug("uart_insert_char: rx disable, data len %d", hci_rx_data_len());
        hci_uart_rx_disable(hci_adapter);
    }
}

static void hciuart_irq(uint32_t id, SerialIrq event)
{
    serial_t    *sobj = (void *)id;
    int max_count = 16;
    uint8_t ch;

    if (event == RxIrq)
    {
        do
        {
            ch = serial_getc(sobj);
            uart_insert_char(hci_uart_obj, ch);
        }
        while (serial_readable(sobj) && max_count-- > 0);

        if (hci_uart_obj->rx_ind)
        {
            hci_uart_obj->rx_ind();
        }
    }
}
static bool hci_uart_malloc(void)
{
    if(hci_uart_obj == NULL)
    {
        hci_uart_obj = os_mem_zalloc(RAM_TYPE_DATA_ON, sizeof(T_HCI_UART)); //reopen not need init uart

        if(!hci_uart_obj)
        {
            hci_board_debug("%s:need %d, left %d\n", __FUNCTION__ ,sizeof(T_HCI_UART),os_mem_peek(RAM_TYPE_DATA_ON));
            return false;
        }
        else
        {
            //ok 
          hci_uart_obj->rx_read_idx = 0;
          hci_uart_obj->rx_write_idx = 0;
        }
    }
    else
    {
        hci_board_debug("%s:rx_buffer not free\n",__FUNCTION__);
        return false;
    }
    return true;
}
static bool hci_uart_free(void)
{
    if(hci_uart_obj == NULL)
    {
        hci_board_debug("%s: hci_uart_obj = NULL, no need free\r\n",__FUNCTION__);
        return false;
    }
    os_mem_free(hci_uart_obj);
    hci_uart_obj = NULL;
    //hci_board_debug("%s: hci_uart_obj  free\r\n",__FUNCTION__);
    return true;
}

//==============================================================
void hci_uart_set_baudrate(uint32_t baudrate)
{
      HCI_PRINT_INFO1("Set baudrate to %d", baudrate);
      hci_board_debug("Set baudrate to %d\n", (int)baudrate);
      serial_baud(&hci_serial_obj, baudrate);
}

bool hci_uart_tx(uint8_t *p_buf, uint16_t len, P_UART_TX_CB tx_cb)
{

      T_HCI_UART *uart_obj = hci_uart_obj;
      
      
      uart_obj->tx_len  = len;
      uart_obj->tx_cb   = tx_cb;
      
#if 1
      serial_send_blocked(&hci_serial_obj, (char *)p_buf,
                                    len,len);
       if (uart_obj->tx_cb)
       {
          uart_obj->tx_cb();
       }

#else
      int ret;
      ret =  serial_send_stream_dma(&hci_serial_obj, (char *)p_buf,
                                    len);
      if (ret != 0)
      {
          hci_board_debug("%s Error(%d)\n", __FUNCTION__, ret);
          return false;
      }
#endif
      return true;
}


bool hci_uart_init(P_UART_RX_CB rx_ind)
{
    if(hci_uart_malloc() != true)
    {
        return false;
    }
    hci_uart_obj->rx_ind = rx_ind;
    
    serial_init(&hci_serial_obj,(PinName) PIN_UART3_TX, (PinName)PIN_UART3_RX);
    serial_baud(&hci_serial_obj, 115200);
    serial_format(&hci_serial_obj, 8, ParityNone, 1);
    hci_serial_obj.uart_adp.base_addr->fcr_b.rxfifo_trigger_level = FifoLvHalf;
   // serial_rx_fifo_level(&comuart_sobj, FifoLvHalf);
    //serial_set_flow_control(&hci_serial_obj, FlowControlRTSCTS, NC, NC);
    serial_set_flow_control(&hci_serial_obj, FlowControlRTS, NC, NC);
    serial_send_comp_handler(&hci_serial_obj, (void *)uart_send_done,
                             (uint32_t)hci_uart_obj);

    serial_clear_rx(&hci_serial_obj);
    serial_irq_handler(&hci_serial_obj, hciuart_irq, (uint32_t)&hci_serial_obj);
    serial_irq_set(&hci_serial_obj, RxIrq, 0);
    serial_irq_set(&hci_serial_obj, RxIrq, 1);
    
    return true;
}

bool hci_uart_deinit(void)
{
    if(hci_uart_obj != NULL)
    {
       serial_free(&hci_serial_obj);
       memset(&hci_serial_obj,0,sizeof(serial_t));
       hci_uart_free();
    }
    else
    {
        hci_board_debug(" %s: deinit call twice  !!!!! \r\n", __FUNCTION__);
        
    }
    return true;
}


uint16_t hci_uart_recv(uint8_t *p_buf, uint16_t size)
{
    uint16_t rx_len;
    
    T_HCI_UART *p_uart_obj = hci_uart_obj;
    //hci_board_debug("hci_uart_recv: write:%d, read:%d,rx_len:%d,need:%d space_len:%d, count:%d\r\n",p_uart_obj->rx_write_idx, p_uart_obj->rx_read_idx, hci_rx_data_len(),size, hci_rx_space_len(), hci_irq_count);
    
    if(p_uart_obj == NULL)
    {
        hci_board_debug(" %s:the p_uart_obj is NULL !!!!! \r\n", __FUNCTION__);
        return 0;
    }
    if(hci_rx_empty())
    {

         //rx empty
         return 0;
    }
    rx_len = hci_rx_data_len();
    
    if (rx_len > size)
    {
        rx_len = size;
    }

    if (rx_len > HCI_UART_RX_BUF_SIZE - p_uart_obj->rx_read_idx)    /* index overflow */
    {
        rx_len = HCI_UART_RX_BUF_SIZE - p_uart_obj->rx_read_idx;
    }

    if (rx_len)
    {
        memcpy(p_buf, &(p_uart_obj->rx_buffer[p_uart_obj->rx_read_idx]), rx_len);

        p_uart_obj->rx_read_idx += rx_len;
        p_uart_obj->rx_read_idx %= HCI_UART_RX_BUF_SIZE;

        if (p_uart_obj->rx_disabled == true)    /* flow control */
        {
            if (hci_rx_data_len() < HCI_UART_RX_ENABLE_COUNT)
            {
                hci_uart_rx_enable(p_uart_obj);
            }
        }
    }

    return rx_len;
}
