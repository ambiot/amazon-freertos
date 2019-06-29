/* mbed Microcontroller Library
 *******************************************************************************
 * Copyright (c) 2014, Realtek
 * All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *******************************************************************************
 */
#include "cmsis.h"
#include "sys_api.h"
#include "flash_api.h"
#include "osdep_service.h"
#include "device_lock.h"
#include "hal_wdt.h"

extern void sys_clear_ota_signature_ext(void); 
extern void sys_recover_ota_signature_ext(void);
extern hal_uart_adapter_t log_uart;
extern void log_uart_port_init (int log_uart_tx, int log_uart_rx, uint32_t baud_rate);

/**
  * @brief  Clear OTA signature so that boot code load default image.
  * @retval none
  */
void sys_clear_ota_signature(void)
{
	sys_clear_ota_signature_ext();
}

/**
  * @brief  Recover OTA signature so that boot code load upgraded image(ota image).
  * @retval none
  */
void sys_recover_ota_signature(void)
{
	sys_recover_ota_signature_ext();
}

/**
  * @brief  system software reset.
  * @retval none
  */
void sys_reset(void)
{
	//hal_misc_cpu_rst();
#if !defined(CONFIG_BUILD_NONSECURE)
	hal_sys_set_fast_boot(NULL, 0);
#endif
	hal_misc_rst_by_wdt();
}

/**
  * @brief  Turn off the JTAG function.
  * @retval none
  */
void sys_jtag_off(void)
{
	hal_misc_jtag_pin_ctrl(0);
}

/**
  * @brief  open log uart.
  * @retval none
  */
void sys_log_uart_on(void)
{
	log_uart_port_init(STDIO_UART_TX_PIN, STDIO_UART_RX_PIN, ((uint32_t)115200));
}

/**
  * @brief  close log uart.
  * @retval none
  */
void sys_log_uart_off(void)
{
	//log_uart_flush_wait();
	//hal_gpio_pull_ctrl (log_uart.rx_pin, Pin_PullNone);
	hal_uart_deinit(&log_uart);
}
