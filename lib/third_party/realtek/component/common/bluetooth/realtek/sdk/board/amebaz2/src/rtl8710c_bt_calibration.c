#include <drv_types.h>
#include <hal_data.h>
#include "rtl8710c_bt_calibration.h"
#include "hal_sys_ctrl.h"

#ifdef CHIP_VER
#undef CHIP_VER  /* CHIP_VER in hal_com_reg.h conflicts with CHIP_VER in platform_conf.h */
#ifdef _PLATFORM_CONF_H_
#undef _PLATFORM_CONF_H_ /* undef _PLATFORM_CONF_H_ to define CHIP_VER in platform_conf.h again */
#include "platform_conf.h"
#endif //#ifdef _PLATFORM_CONF_H_
#endif //#ifdef CHIP_VER

#include "flash_api.h"

#define DBG_BT_CALIBRATION				(0) // Debug log, default close

#define FLASH_BT_PARA_ADDR				(SYS_DATA_FLASH_BASE + 0xFF0)
#define BT_IQK_CANDIDATE_NUM			(3)
#define BT_IQK_MAX_RETRY				(2)
#define BT_DCK_TOLERANCE				(1) // Suggested by RDC BiChing
#define BT_IQK_TOLERANCE				(2) // Suggested by BT Jeffery
#define BT_IQK_BACKUP_BB_REG_NUM		(8)
#define BT_IQK_BACKUP_RF_REG_NUM		(3)
#define BT_IQK_DELAY_TIME				(20)
#define BT_DCK_BIT_LEN					(9)
#define BT_IQK_BIT_LEN					(10)
#define BT_LOK_BIT_LEN					(6)

extern int GNT_BT_to_wifi(void);
extern void disable_pta(void);
extern Rltk_wlan_t	rltk_wlan_info[NET_IF_NUM];

void _bt_backup_bb_registers_8710c(struct dm_struct *dm,
								  uint32_t *reg,
								  uint32_t *backup_val,
								  uint32_t register_num)
{
	for (uint32_t i = 0; i < register_num; i++)
		backup_val[i] = odm_get_bb_reg(dm, reg[i], MASKDWORD);
}

void _bt_backup_rf_registers_8710c(struct dm_struct *dm,
								  uint32_t *reg,
								  uint32_t *backup_val,
								  uint32_t register_num)
{
	for (uint32_t i = 0; i < register_num; i++)
		backup_val[i] = odm_get_rf_reg(dm, RF_PATH_A, reg[i], RFREG_MASK);
}

void _bt_reload_bb_registers_8710c(struct dm_struct *dm,
								  uint32_t *reg,
								  uint32_t *backup_val,
								  uint32_t register_num)
{
	for (uint32_t i = 0; i < register_num; i++)
		odm_set_bb_reg(dm, reg[i], MASKDWORD, backup_val[i]);
}

void _bt_reload_rf_registers_8710c(struct dm_struct *dm,
								  uint32_t *reg,
								  uint32_t *backup_val,
								  uint32_t register_num)
{
	for (uint32_t i = 0; i < register_num; i++)
		odm_set_rf_reg(dm, RF_PATH_A, reg[i], RFREG_MASK, backup_val[i]);
}


int16_t signed_value_conversion(uint16_t val, uint8_t bit_len)
{
	int16_t sign_bit = 1 << (bit_len-1);
	int16_t value_mask = (1 << (bit_len-1)) - 1;
	if (val & sign_bit) { // val < 0
		int16_t twos_complement_val = ((~val) & value_mask) + 1;
		return -(twos_complement_val);
	}
	else
		return val;
}


BT_Cali_TypeDef calibration_data_conversion_struct(uint64_t result_u64)
{
	BT_Cali_TypeDef result;	
	result.IQK_xx = (result_u64 >> 34) & 0x3FF;
	result.IQK_yy = (result_u64 >> 24) & 0x3FF;
	result.IDAC = (result_u64 >> 18) & 0x3F;
	result.QDAC = (result_u64 >> 12) & 0x3F;
	result.IDAC2 = (result_u64 >> 6) & 0x3F;
	result.QDAC2 = (result_u64 >> 0) & 0x3F;
	return result;
}

uint64_t calibration_data_conversion_u64(BT_Cali_TypeDef cal_data)
{
	uint64_t result = ((uint64_t)cal_data.IQK_xx << 34) | ((uint64_t)cal_data.IQK_yy << 24) | (cal_data.IDAC << 18) | (cal_data.QDAC << 12) | (cal_data.IDAC2 << 6) | (cal_data.QDAC2);
	return result;
}

void sort_iqk(uint32_t* iqk_array, int num)
{
	uint32_t temp;
	int i, j;
	int16_t val_i, val_j;
	
	for (i = 0; i < (num - 1); i++) {
		val_i = signed_value_conversion(iqk_array[i], BT_IQK_BIT_LEN);
		for (j = (i + 1); j < num; j++) {
			val_j = signed_value_conversion(iqk_array[j], BT_IQK_BIT_LEN);
			if (val_i > val_j) {
				temp = iqk_array[i];
				iqk_array[i] = iqk_array[j];
				iqk_array[j] = temp;
				val_i = signed_value_conversion(iqk_array[i], BT_IQK_BIT_LEN);
			}
		}
	}
}

void sort_lok(uint16_t* lok_array, int num)
{
	uint16_t temp;
	int i, j;
	
	for (i = 0; i < (num - 1); i++) {
		for (j = (i + 1); j < num; j++) { 
			if (lok_array[i] > lok_array[j]) {
				temp = lok_array[i];
				lok_array[i] = lok_array[j];
				lok_array[j] = temp;
			}
		}
	}
}


bool bt_calibration_similar(uint16_t a, uint16_t b, uint8_t bit_len, uint8_t tolerance, bool signed_val)
{
	int16_t val_a = (signed_val)? signed_value_conversion(a, bit_len): a;
	int16_t val_b = (signed_val)? signed_value_conversion(b, bit_len): b;
	
	uint16_t diff = (val_a > val_b) ? (val_a - val_b) : (val_b - val_a);
	
	if (diff <= tolerance) {
		return true;
	}
	else {
		return false;
	}
}

uint64_t phy_bt_iqk_lok_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	uint32_t btiqk_x, btiqk_y, btlok_x, btlok_y, btlok2_x, btlok2_y;
	uint64_t result;
	uint32_t t = 0;
	
	/*1.AmebaZII_TXLOKIQK_TEST.txt */
	//BB setting, save original value
	odm_set_bb_reg(dm, R_0x88c, 0x00f00000, 0xf);
	odm_set_bb_reg(dm, R_0x874, MASKDWORD, 0x25205000);	//[21] 0x1
	odm_set_bb_reg(dm, R_0xc08, MASKDWORD, 0x800e4);	//[19] 0x1
	odm_set_bb_reg(dm, R_0xc04, MASKDWORD, 0x03a05601);
	
	// AFE set BT mode
	odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0x000000cb);
	
	odm_set_bb_reg(dm, R_0xe28, BIT31, 0x0);
	odm_set_bb_reg(dm, R_0xe28, BIT23, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xdf, BIT1, 0x0);			// WIFI TX Gain debug
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x56, 0x00000fff, 0xee8);	// WIFI TX Gain [11:0]
	
	// ===========================
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xde, BIT16, 0x1);		// BT TX Gain debug
	odm_set_rf_reg(dm, RF_PATH_A, 0x06, 0x00003000, 0x3);		// DAC Gain [13:12]
	odm_set_rf_reg(dm, RF_PATH_A, 0x06, 0x00000E00, 0x2);		// IPA [11:9]
	odm_set_rf_reg(dm, RF_PATH_A, 0x06, 0x000001E0, 0x3);		// PAD [8:5]
	odm_set_rf_reg(dm, RF_PATH_A, 0x06, 0x0000001F, 0x2);		// TXBB [4:0] // 20190610: BT Jeffery: mapping TX gain table 4.5dBm
	// ===========================
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT3, 0x1);
	//ADDA on//
	odm_set_bb_reg(dm, R_0xe70, MASKDWORD, 0x03c00016);
	//-----------------------------//
	
	//IQK & LOK setting
	odm_set_bb_reg(dm, R_0xe28, MASKDWORD, 0x00800044);	//e28[23]:iqk-adda
	//path-A IQK setting
	odm_set_bb_reg(dm, R_0xe30, MASKDWORD, 0x1800cc08);	//tx-tone-id[9:0] , TX-cal-M[29]
	odm_set_bb_reg(dm, R_0xe34, MASKDWORD, 0x3800cc08);	//rx-tone-id[9:0] , RX-cal-M[29]
	odm_set_bb_reg(dm, R_0xe50, MASKDWORD, 0x3800cc08);	//tx-tone-id[9:0] , TX-cal-M[29]
	odm_set_bb_reg(dm, R_0xe54, MASKDWORD, 0x3800cc08);	//rx-tone-id[9:0] , RX-cal-M[29]
	odm_set_bb_reg(dm, R_0xe38, MASKDWORD, 0x821403e8);	//TX-PI-data 
	odm_set_bb_reg(dm, R_0xe3c, MASKDWORD, 0x68160c06);	//RX-PI-data
	odm_set_bb_reg(dm, R_0xe40, MASKDWORD, 0x01007c00);	//tx-stpz=01, X=1,Y=0, TX-XY-Mask[31] 
	odm_set_bb_reg(dm, R_0xe44, MASKDWORD, 0x01004800);	//rx-stpz=00, X=1,Y=0, RX-XY-Mask[31]
	odm_set_bb_reg(dm, R_0xe4c, MASKDWORD, 0x00462911);	//idac_cal_msk:4c[15]
	
	// ====== Set BTRF =====
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x18, 0x00003000, 0x0); //[17:16]
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, MASKDWORD, 0x00040);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x1, BIT2, 0x1);
	odm_set_rf_reg(dm, RF_PATH_A, 0x20, BIT5, 0x1);
	// ====== Set BTRF =====
	
	odm_set_bb_reg(dm, R_0xe28, MASKDWORD, 0x80800044);	//e28[23]:iqk-adda
	// ----- one shot -----//
	odm_set_bb_reg(dm, R_0xe48, MASKDWORD, 0xf9000800);
	odm_set_bb_reg(dm, R_0xe48, MASKDWORD, 0xf8000800);
	//---------------------//
	
	ODM_delay_ms(16);
	while ((odm_get_bb_reg(dm, R_0xeac, BIT26) != 1) && (t < 20000)) {
		ODM_delay_us(50);
		t += 50;
	}
	
	/*12_Read_Tx_X_Tx_Y*/
	btiqk_x = odm_get_bb_reg(dm, R_0xe94, 0x03FF0000);	//r_TX_X : e94[25:16]
	btiqk_y = odm_get_bb_reg(dm, R_0xe9c, 0x03FF0000);	//r_TX_Y : e9c[25:16]
	
	RF_DBG(dm, DBG_RF_IQK, "btiqk_x = 0x%x, btiqk_y = 0x%x, iqkend = 0x%x/n", 
		btiqk_x,
		btiqk_y,
		odm_get_bb_reg(dm, R_0xeac, BIT26));
	
	/*13_Read_IQDAC */
	odm_set_bb_reg(dm, R_0xe28, MASKDWORD, 0x800044);  //e28[23]:iqk-adda
	btlok_x = odm_get_rf_reg(dm, RF_PATH_A, RF_0x08, 0xFC000); //[19:14]
	btlok_y = odm_get_rf_reg(dm, RF_PATH_A, RF_0x08, 0x003F0); //[9:4]
	
	// second LOK
	odm_set_rf_reg(dm, RF_PATH_A, 0x06, 0x1E0, 0x6);       //PAD[8:5]
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, MASKDWORD, 0x0);
	odm_set_bb_reg(dm, R_0xe28, MASKDWORD, 0x80800044);  //e28[23]:iqk-adda
	// ----- one shot -----//
	odm_set_bb_reg(dm, R_0xe48, MASKDWORD, 0xf9000800);
	odm_set_bb_reg(dm, R_0xe48, MASKDWORD, 0xf8000800);
	//---------------------//
	
	t = 0;
	ODM_delay_ms(16);
	while ((odm_get_bb_reg(dm, R_0xeac, BIT26) != 1) && (t < 20000)) {
		ODM_delay_us(50);
		t += 50;
	}
	odm_set_bb_reg(dm, R_0xe28, MASKDWORD, 0x800044);  //e28[23]:iqk-adda
	btlok2_x = odm_get_rf_reg(dm, RF_PATH_A, RF_0x08, 0xFC000); //[19:14]
	btlok2_y = odm_get_rf_reg(dm, RF_PATH_A, RF_0x08, 0x003F0); //[9:4]
	
	/*14_restore_BTRF_setting_20181024*/
	odm_set_bb_reg(dm, R_0x88c, MASKDWORD, 0xcc0400c0); // 20190306 Added by BT Allen Hsu
	odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0x00000003); //ad and da config to WIFI
	
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xde, BIT16, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x1, BIT2, 0x0);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT3, 0x0);
	
	odm_set_bb_reg(dm, R_0xc08, MASKDWORD, 0x000000e4);	// 20190326, Added by RDC Hillo
	odm_set_bb_reg(dm, R_0xc04, MASKDWORD, 0x03a05611); // 20190326, Added by RDC Hillo
	
	result = ((uint64_t)btiqk_x << 34) | ((uint64_t)btiqk_y << 24) | (btlok_x << 18) | (btlok_y << 12) | (btlok2_x << 6) | (btlok2_y);
	return result;
}

void phy_bt_lok_write_8710c(struct dm_struct *dm, uint16_t idac, uint16_t qdac, uint16_t idac2, uint16_t qdac2)
{
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT3, 0x1);			//LOK Write EN 0xEF[3]=1b
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, BIT6, 0x1);			//LOK for BT -> 0x33[6]=1b
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x08, 0xfc000, idac);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x08, 0x003f0, qdac);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x33, BIT6, 0x0);			//LOK2 for BT -> 0x33[6]=0b
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x08, 0xfc000, idac2);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0x08, 0x003f0, qdac2);
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xef, BIT3, 0x0);			//Close LOK Write EN
}

void phy_bt_adda_dck_8710c(void *dm_void)
{
	//printf("\n\rphy_bt_adda_dck_8710c---------------->\n\r");
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	uint32_t t = 0, dck_cnt = 0;
	uint8_t dck_ok = 0;
	uint16_t adci, adcq, dai_adci, daq_adcq;
	
	while (!dck_ok) {
		RF_DBG(dm, DBG_RF_IQK, "[BT DCK]ADC offset mark off!!");
		
		/*01_DAC_DCK_ADC_offset_mark_off_20181024*/
		odm_set_bb_reg(dm, R_0x88c, MASKDWORD, 0xccf000c0);
		odm_set_bb_reg(dm, R_0xc80, MASKDWORD, 0x40000100);
		odm_set_bb_reg(dm, R_0xe6c, MASKDWORD, 0x03c00016);
		odm_set_bb_reg(dm, R_0xe70, MASKDWORD, 0x03c00016);
		odm_set_bb_reg(dm, R_0xc04, MASKDWORD, 0x03a05601);
		odm_set_bb_reg(dm, R_0xc08, MASKDWORD, 0x000800e4);
		odm_set_bb_reg(dm, R_0x874, MASKDWORD, 0x25204000);
		odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0x0000008b);
		
		odm_set_bb_reg(dm, R_0x950, 0x01ff01ff, 0x00);
		odm_set_bb_reg(dm, R_0x818, 0x00400000, 0x0);
		odm_set_bb_reg(dm, R_0x990, 0x40000000, 0x1);
		
		odm_set_bb_reg(dm, R_0x880, 0x00000002, 0x0);	// RX BB to AD off
		ODM_delay_ms(25);  // without this delay, BT LOK may fail (still don't know why) (Debugged with RDC BiChing)
		
		odm_set_bb_reg(dm, R_0x880, 0x00000010, 0x1);
		odm_set_bb_reg(dm, R_0x880, 0x00008000, 0x1);
		odm_set_bb_reg(dm, R_0x880, 0x00010000, 0x0);
		odm_set_bb_reg(dm, R_0x880, 0x00020000, 0x1);
		odm_set_bb_reg(dm, R_0x888, 0x00000020, 0x0);
		
		odm_set_bb_reg(dm, R_0xe28, 0x00000008, 0x1);
		odm_set_bb_reg(dm, R_0xe48, 0x00f00000, 0x1);
		odm_set_bb_reg(dm, R_0xe48, 0x00000002, 0x0);
		odm_set_bb_reg(dm, R_0x908, 0x00000fff, 0x300);
		
		odm_set_bb_reg(dm, R_0x990, 0x80000000, 0x0);
		
		ODM_delay_us(1);
		
		odm_set_bb_reg(dm, R_0x990, 0x80000000, 0x1);
		
		// read 0xdf4[9:1]    ADCI offset
		adci = odm_get_bb_reg(dm, R_0xdf4, 0x000003FE);
		// read 0xdf4[31:23]  ADCQ offset
		adcq = odm_get_bb_reg(dm, R_0xdf4, 0xFF800000);
		
		RF_DBG(dm, DBG_RF_IQK, "[BT DCK]BT DAC mark off!!");
		
		/*04_DAC_DCK_mark_off_20181024*/
		odm_set_bb_reg(dm, R_0x88c, MASKDWORD, 0xccf000c0);
		odm_set_bb_reg(dm, R_0xc80, MASKDWORD, 0x40000100);
		odm_set_bb_reg(dm, R_0xe6c, MASKDWORD, 0x03c00016);
		odm_set_bb_reg(dm, R_0xe70, MASKDWORD, 0x03c00016);
		odm_set_bb_reg(dm, R_0xc04, MASKDWORD, 0x03a05601);
		odm_set_bb_reg(dm, R_0xc08, MASKDWORD, 0x000800e4);
		odm_set_bb_reg(dm, R_0x874, MASKDWORD, 0x25204000);
		odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0x0000008b);
		
		odm_set_bb_reg(dm, R_0x818, 0x00400000, 0x0);
		odm_set_bb_reg(dm, R_0x990, 0x40000000, 0x1);
		
		odm_set_bb_reg(dm, R_0x880, 0x00000002, 0x0);
		odm_set_bb_reg(dm, R_0x880, 0x00000010, 0x0);
		odm_set_bb_reg(dm, R_0x880, 0x00008000, 0x0);
		odm_set_bb_reg(dm, R_0x880, 0x00010000, 0x1);
		odm_set_bb_reg(dm, R_0x880, 0x00020000, 0x1);
		odm_set_bb_reg(dm, R_0x888, 0x00000020, 0x1);
		
		ODM_delay_us(1);
		
		odm_set_bb_reg(dm, 0x988, MASKDWORD, 0x0003FFF0);
		odm_set_bb_reg(dm, R_0x98c, MASKDWORD, 0x00000000);
		odm_set_bb_reg(dm, R_0x994, MASKDWORD, 0x0003FFF0);
		odm_set_bb_reg(dm, R_0x998, MASKDWORD, 0x00000000);
		
		odm_set_bb_reg(dm, 0x988, 0x80000000, 0x1);
		odm_set_bb_reg(dm, R_0x994, 0x80000000, 0x1);
		
		odm_set_bb_reg(dm, 0x988, 0x40000000, 0x0);
		odm_set_bb_reg(dm, R_0x994, 0x40000000, 0x0);
		
		odm_set_bb_reg(dm, 0x988, 0x00000001, 0x1);	//pow_da_I_CR
		odm_set_bb_reg(dm, R_0x994, 0x00000001, 0x1);	//pow_da_Q_CR
		
		odm_set_bb_reg(dm, 0x988, 0x00000002, 0x1);	//cal_os_I_CR
		odm_set_bb_reg(dm, R_0x994, 0x00000002, 0x1);	//cal_os_Q_CR
		
		ODM_delay_us(350);
		while ((odm_get_bb_reg(dm, 0xfec, BIT31) != 1) && (t < 1000)) {
			ODM_delay_us(5);
			t += 5;
		}
		
		odm_set_bb_reg(dm, 0x988, 0x00000001, 0x0);	//pow_da_I_CR
		
		while ((odm_get_bb_reg(dm, 0xff8, BIT31) != 1) && (t < 1000)) {
			ODM_delay_us(5);
			t += 5;
		}
		odm_set_bb_reg(dm, R_0x994, 0x00000001, 0x0);	//pow_da_Q_CR
		
		// read 0xdf4[9:1]    ADCI offset
		dai_adci = odm_get_bb_reg(dm, R_0xdf4, 0x000003FE);
		// read 0xdf4[31:23]  ADCQ offset
		daq_adcq = odm_get_bb_reg(dm, R_0xdf4, 0xFF800000);
		
		/*return to normal setting*/
		odm_set_bb_reg(dm, R_0x880, 0x00000002, 0x1);
		odm_set_bb_reg(dm, R_0x880, 0x00000010, 0x1);
		odm_set_bb_reg(dm, R_0x880, 0x00010000, 0x0);
		odm_set_bb_reg(dm, R_0x880, 0x00020000, 0x0);
		odm_set_bb_reg(dm, R_0x888, 0x00000020, 0x0);
		
		odm_set_bb_reg(dm, R_0x888, MASKDWORD, 0x00000003);
		
		dck_cnt++;
		
		if ((bt_calibration_similar(adci, dai_adci, BT_DCK_BIT_LEN, BT_DCK_TOLERANCE, true))
			 && (bt_calibration_similar(adcq, daq_adcq, BT_DCK_BIT_LEN, BT_DCK_TOLERANCE, true)))
		{
#if DBG_BT_CALIBRATION
			printf("\n\r[BT DCK][%d] DCK OK: ", dck_cnt);
			printf("ADCI = %x, ADCQ = %x, DAI_ACDI = %x, DAQ_ADCQ = %x",
				   adci, adcq, dai_adci, daq_adcq);
#endif
			dck_ok = 1;
		}
		else {
#if DBG_BT_CALIBRATION
			printf("\n\r***********************[DCK Error]***********************");
			printf("\n\r[BT DCK][%d] DCK fail: ", dck_cnt);
			printf("ADCI = %x, ADCQ = %x, DAI_ACDI = %x, DAQ_ADCQ = %x",
				   adci, adcq, dai_adci, daq_adcq);
#endif
			dck_ok = 0;
			if (dck_cnt == 100) {
				printf("\n\r[BT DCK] DCK Error 100 times. Please reboot.");
				while (1);
			}
		}
	}
#if DBG_BT_CALIBRATION
	printf("\n\r[BT DCK] DCK run %d time(s) %s", dck_cnt, dck_ok? "OK":"FAIL");
#endif
}


void phy_bt_dck_write_8710c(void *dm_void, uint8_t q_dck, uint8_t i_dck)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	
	odm_set_bb_reg(dm, R_0x998, 0x00000001, 0x1);
	odm_set_bb_reg(dm, R_0x994, 0x00000001, 0x1);
	
	odm_set_bb_reg(dm, R_0x98c, 0x01000000, 1);
	odm_set_bb_reg(dm, R_0x998, 0x01000000, 1);
	odm_set_bb_reg(dm, R_0x98c, 0x00200000, 1);
	odm_set_bb_reg(dm, R_0x998, 0x00200000, 1);
	odm_set_bb_reg(dm, R_0x98c, 0x02000000, 1);
	odm_set_bb_reg(dm, R_0x998, 0x02000000, 1);
	
	odm_set_bb_reg(dm, R_0x98c, 0x00007c00, 0x1f);
	odm_set_bb_reg(dm, R_0x98c, 0x00000200, 0x1);
	
	odm_set_bb_reg(dm, R_0x98c, 0x001F8000, i_dck);
	
	odm_set_bb_reg(dm, R_0x998, 0x00007c00, 0x1f);
	odm_set_bb_reg(dm, R_0x998, 0x00000200, 0x1);
	
	odm_set_bb_reg(dm, R_0x998, 0x001F8000, q_dck);
	
	odm_set_bb_reg(dm, R_0x998, 0x00000001, 0x0);
	odm_set_bb_reg(dm, R_0x994, 0x00000001, 0x0);
}

uint32_t bt_dck_write(uint8_t q_dck, uint8_t i_dck)
{
	struct net_device *dev;
	_adapter *padapter;
	PHAL_DATA_TYPE pHalData;
	u32 ret = _SUCCESS;
	
	dev = rltk_wlan_info[0].dev;
	if(dev) {
		padapter = (_adapter *)rtw_netdev_priv(dev);
		pHalData = GET_HAL_DATA(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		if ( Hal_BT_Is_Supported(padapter) ) {
			phy_bt_dck_write_8710c(&(pHalData->odmpriv),q_dck,i_dck);
		}
		else {
			ret = _FAIL;
			DBG_ERR("BT not supported\n\r");
		}
	} else {
		ret = _FAIL;
		DBG_ERR("netif is DOWN");
	}

	return ret;
}

uint32_t bt_adda_dck_8710c(void)
{
	struct net_device *dev;
	_adapter *padapter;
	PHAL_DATA_TYPE pHalData;
	uint32_t ret = _SUCCESS;
	
	dev = rltk_wlan_info[0].dev;
	if(dev) {
		padapter = (_adapter *)rtw_netdev_priv(dev);
		pHalData = GET_HAL_DATA(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		if ( Hal_BT_Is_Supported(padapter) ) {
			phy_bt_adda_dck_8710c(&(pHalData->odmpriv));
		}
		else {
			ret = _FAIL;
			DBG_ERR("BT not supported\n\r");
		}
	}
	else {
		ret = _FAIL;
		DBG_ERR("netif is DOWN");
	}
	return ret;
}
uint32_t bt_lok_write(uint16_t idac, uint16_t qdac, uint16_t idac2, uint16_t qdac2)
{
	struct net_device *dev;
	_adapter *padapter;
	PHAL_DATA_TYPE pHalData;
	uint32_t ret = _SUCCESS;
	
	dev = rltk_wlan_info[0].dev;
	if(dev) {
		padapter = (_adapter *)rtw_netdev_priv(dev);
		pHalData = GET_HAL_DATA(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		if ( Hal_BT_Is_Supported(padapter) ) {
			phy_bt_lok_write_8710c(&(pHalData->odmpriv), idac, qdac, idac2, qdac2);
		}
		else {
			phy_bt_lok_write_8710c(&(pHalData->odmpriv), 0, 0, 0, 0);
			ret = _FAIL;
			DBG_ERR("BT not supported\n\r");
		}
	}
	else {
		ret = _FAIL;
		DBG_ERR("netif is DOWN");
	}
	
	return ret;
}

bool select_iqk(uint32_t* iqk_array, int num, uint32_t* selected)
{
	uint8_t median_idx = (num/2);
	sort_iqk(iqk_array, num);
	
	if (bt_calibration_similar(iqk_array[0], iqk_array[num-1], BT_IQK_BIT_LEN, BT_IQK_TOLERANCE, true))
	{
		*selected = iqk_array[median_idx];
		return true;
	}
	else {
		return false;
	}
}

bool select_lok(uint16_t* lok_array, int num, uint16_t* selected)
{
	uint8_t median_idx = (num/2);
	sort_lok(lok_array, num);
	
	if (bt_calibration_similar(lok_array[0], lok_array[num-1], BT_LOK_BIT_LEN, BT_IQK_TOLERANCE, false))
	{
		*selected = lok_array[median_idx];
		return true;
	}
	else {
		return false;
	}
}

uint64_t bt_iqk_lok_8710c(void *dm_void)
{
	uint64_t result_u64;
	uint64_t selected_u64;
	int candidate_num = BT_IQK_CANDIDATE_NUM;
	BT_Cali_TypeDef result[BT_IQK_CANDIDATE_NUM];
	BT_Cali_TypeDef defaul_value = {0x100,0x00,0x20,0x20,0x20,0x20}; // default value
	BT_Cali_TypeDef selected = defaul_value;
	uint32_t iqk_array[2][BT_IQK_CANDIDATE_NUM]; // IQK_XX, IQK_YY
	uint16_t lok_array[4][BT_IQK_CANDIDATE_NUM]; // QDAC, IDAC, QDAC2, IDAC2
	bool calibration_valid[6] = {true, true, true, true, true, true};			// IQK_XX, IQK_YY, QDAC, IDAC, QDAC2, IDAC2
	bool calibration_status[6] = {false, false, false, false, false, false};	// IQK_XX, IQK_YY, QDAC, IDAC, QDAC2, IDAC2
	bool calibration_all_vaid = false;
	const char *name[]  = {"IQK_xx","IQK_yy","QDAC","IDAC","QDAC2","IDAC2"};
	int i=0, iqk_cnt = 0;
	while ( (!calibration_all_vaid) && (iqk_cnt < BT_IQK_MAX_RETRY) ) {
		// Do calibration
		iqk_cnt ++;
		for(i=0; i < BT_IQK_CANDIDATE_NUM; i++) {
			result_u64 = phy_bt_iqk_lok_8710c(dm_void);
			result[i] = calibration_data_conversion_struct(result_u64);
#if DBG_BT_CALIBRATION
			printf("\n\r[BT IQK][%d] IQK_xx = 0x%x, IQK_yy = 0x%x, QDAC = 0x%x, IDAC = 0x%x, QDAC2 = 0x%x, IDAC2 = 0x%x",
				   i, result[i].IQK_xx, result[i].IQK_yy, result[i].QDAC, result[i].IDAC, result[i].QDAC2, result[i].IDAC2);
#endif
			iqk_array[0][i] = result[i].IQK_xx;
			iqk_array[1][i] = result[i].IQK_yy;
			lok_array[0][i] = result[i].QDAC;
			lok_array[1][i] = result[i].IDAC;
			lok_array[2][i] = result[i].QDAC2;
			lok_array[3][i] = result[i].IDAC2;
		}
		
		// Check IQK candidate range <= BT_IQK_TOLERANCE indivdually
		for(i = 0; i < 2; i++) {
			if (calibration_status[i] == false) {
				uint32_t* iqk_ptr = (i==0)? (&selected.IQK_xx) : (&selected.IQK_yy);
				calibration_status[i] = select_iqk(iqk_array[i], BT_IQK_CANDIDATE_NUM, iqk_ptr);
			}
		}
		
		// Check LOK candidate range <= BT_IQK_TOLERANCE indivdually
		for(i = 0; i < 4; i++) {
			if (calibration_status[i+2] == false) {
				uint16_t* lok_ptr = (i==0)? (&selected.QDAC) :
									(i==1)? (&selected.IDAC) :
									(i==2)? (&selected.QDAC2) : (&selected.IDAC2);
				calibration_status[i+2] = select_lok(lok_array[i], candidate_num, lok_ptr);
			}
		}
		
		if (!memcmp(calibration_status, calibration_valid, sizeof(calibration_status))) {
			// All calibration done, exit retry loop
			calibration_all_vaid = true;
			break;
		}
		else {
#if DBG_BT_CALIBRATION
			printf("\n\r***********************[IQK FAIL]***********************");
			/* Debug MSG */
			for(i=0; i<6; i++) {
				if (!calibration_status[i]) {
					printf("\n\r[BT IQK][%d] %s fail", i, name[i]); 
				}
			}
#endif
		}
	}
	
	if (!calibration_all_vaid) {
#if DBG_BT_CALIBRATION
		printf("\n\r***********************[_IQK_]***********************");
#endif
		for(i=0; i<6; i++) {
			if (!calibration_status[i]) {
				printf("\n\r[BT IQK][Warning] %s fail, use default value", name[i]); 
			}
		}
	}
#if DBG_BT_CALIBRATION
	printf("\n\r[BT IQK][Selected] IQK_xx = 0x%x, IQK_yy = 0x%x, QDAC = 0x%x, IDAC = 0x%x, QDAC2 = 0x%x, IDAC2 = 0x%x",
		   selected.IQK_xx, selected.IQK_yy, selected.QDAC, selected.IDAC, selected.QDAC2, selected.IDAC2);
#endif	
	selected_u64 = calibration_data_conversion_u64(selected);

	return selected_u64;
}

uint64_t phy_bt_iqk_8710c(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	uint64_t result;
	
	uint32_t bb_backup_reg[BT_IQK_BACKUP_BB_REG_NUM] = {R_0x88c, R_0x880, R_0x888, R_0xc08, R_0xc04, R_0x908, R_0x950, 0x074};
	uint32_t bb_backup_val[BT_IQK_BACKUP_BB_REG_NUM];
	
	uint32_t rf_backup_reg[BT_IQK_BACKUP_RF_REG_NUM] = {RF_0xde, RF_0x1, RF_0xef};
	uint32_t rf_backup_val[BT_IQK_BACKUP_RF_REG_NUM];
	
	RF_DBG(dm, DBG_RF_IQK, "[BT_IQK] Start IQK LOK");
	
	_bt_backup_bb_registers_8710c(dm, bb_backup_reg, bb_backup_val, BT_IQK_BACKUP_BB_REG_NUM);
	_bt_backup_rf_registers_8710c(dm, rf_backup_reg, rf_backup_val, BT_IQK_BACKUP_RF_REG_NUM);
	
	GNT_BT_to_wifi();
	disable_pta();
	
	RF_DBG(dm, DBG_RF_IQK, "[BT_IQK] Start DCK");
	//phy_bt_adda_dck_8710c(dm);
	
	ODM_delay_ms(BT_IQK_DELAY_TIME);
	result = bt_iqk_lok_8710c(dm);
	
	_bt_reload_bb_registers_8710c(dm, bb_backup_reg, bb_backup_val, BT_IQK_BACKUP_BB_REG_NUM);
	_bt_reload_rf_registers_8710c(dm, rf_backup_reg, rf_backup_val, BT_IQK_BACKUP_RF_REG_NUM);
	RF_DBG(dm, DBG_RF_IQK, "[BT_IQK] IQK LOK end");
	
	return result;
}

uint32_t bt_iqk_8710c(BT_Cali_TypeDef *cal_data, BOOLEAN store)
{
	//printf("\n\rbt_iqk_8710c---------------->\n\r");
	struct net_device *dev;
	_adapter *padapter;
	PHAL_DATA_TYPE pHalData;
	uint64_t result;
	uint32_t ret = _SUCCESS;
	flash_t flash;
	
	dev = rltk_wlan_info[0].dev;
	if(dev) {
		padapter = (_adapter *)rtw_netdev_priv(dev);
		pHalData = GET_HAL_DATA(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		if ( Hal_BT_Is_Supported(padapter) ) {
			result = phy_bt_iqk_8710c(&(pHalData->odmpriv));
			//get calibration data
			if(cal_data) {
				*cal_data = calibration_data_conversion_struct(result);
			}
			
			if(store) {
				if((HAL_READ32(SPI_FLASH_BASE, FLASH_BT_PARA_ADDR) == 0xFFFFFFFF) && (cal_data))
					flash_stream_write(&flash, FLASH_BT_PARA_ADDR, sizeof(BT_Cali_TypeDef), (uint8_t*)cal_data);
				else
					ret =_FAIL;
			}
		}
		else {
			ret = _FAIL;
			DBG_ERR("BT not supported\n\r");
		}
	}
	else {
		ret =_FAIL;
		DBG_ERR("netif is DOWN");
	}
	return ret;
}

void phy_bt_flatk_8710c(struct dm_struct *dm, uint16_t txgain_flatk)
{
	uint8_t txgain_flak_use;
	uint8_t txgaink_flag;
	uint8_t temp = 0;
	
	txgaink_flag = (signed_value_conversion((txgain_flatk>>0&0x0f), 4) <= 2) &&
		(signed_value_conversion((txgain_flatk>>4&0x0f), 4) <= 2) &&
		(signed_value_conversion((txgain_flatk>>8&0x0f), 4) <= 2) &&
		(signed_value_conversion((txgain_flatk>>12&0x0f), 4) <= 2);

	odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT17, 0x1);
	for(int i = 0; i<4; i++) {
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x30, BIT(12)|BIT(13), i);
		txgain_flak_use = (txgain_flatk>>(i*4)) & 0x0f ;
		if(txgaink_flag)
			txgain_flak_use +=2;
		if(txgain_flak_use == 0x07)
			txgain_flak_use =0x06;
		temp = (((txgain_flak_use>>1) + (txgain_flak_use&0x01)) & 0x07);
		odm_set_rf_reg(dm, RF_PATH_A, RF_0x30, BIT2|BIT1|BIT0, temp);
	}
	odm_set_rf_reg(dm, RF_PATH_A, RF_0xee, BIT17, 0x0);
}

uint32_t bt_flatk_8710c(uint16_t txgain_flatk)
{
	//printf("\n\rbt_flatk_8710c---------------->\n\r");
	struct net_device *dev;
	_adapter *padapter;
	PHAL_DATA_TYPE pHalData;
	uint64_t result;
	uint32_t ret = _SUCCESS;
	flash_t flash;
	
	dev = rltk_wlan_info[0].dev;
	if(dev) {
		padapter = (_adapter *)rtw_netdev_priv(dev);
		pHalData = GET_HAL_DATA(padapter);
		LeaveAllPowerSaveMode(padapter);
		
		if ( Hal_BT_Is_Supported(padapter) ) {
			phy_bt_flatk_8710c(&(pHalData->odmpriv), txgain_flatk);
		}
		else {
			ret = _FAIL;
			DBG_ERR("BT not supported\n\r");
		}
	}
	else {
		ret =_FAIL;
		DBG_ERR("netif is DOWN");
	}
	return ret;
}
