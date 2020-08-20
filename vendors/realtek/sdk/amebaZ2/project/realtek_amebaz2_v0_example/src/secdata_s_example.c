/**************************************************************************//**
 * @file     secdata_example.c
 * @brief    Demo user defined flash secure data protection.
 * @version  V1.00
 * @date     2019-07-09
 *
 * @note
 *
 ******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation. All rights reserved.
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
#include <string.h>
#include <stdint.h>
#include <cmsis.h>
#include <hal_efuse.h>
#include <crypto_api.h>

typedef struct sec_data_s{
	uint8_t* data;
	int len;	// string using 0, using strlen to detect string size
}sec_data_t;

typedef struct sec_head_s{
	int data_num;
	int data_len_array[1];
}sec_head_t;

static sec_data_t *sec_data_array;
static int sec_data_num;

#define USE_CBC 1
#define USE_GCM 2
#define DEMO_ALGORITHM USE_GCM		

#define SECDATA_SIZE	4096
static uint8_t secdata_data[SECDATA_SIZE] __attribute__((aligned(32)));


// vector from NIST: AES-GCM 256 bits :
/*
uint8_t aes_256_demo_key[32] __attribute__((aligned(32))) =
{
    0x58, 0x53, 0xc0, 0x20, 0x94, 0x6b, 0x35, 0xf2,
    0xc5, 0x8e, 0xc4, 0x27, 0x15, 0x2b, 0x84, 0x04,
    0x20, 0xc4, 0x00, 0x29, 0x63, 0x6a, 0xdc, 0xbb,
    0x02, 0x74, 0x71, 0x37, 0x8c, 0xfd, 0xde, 0x0f
};
*/
uint8_t aes_256_demo_key[32] __attribute__((aligned(32))) =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

const unsigned char aes_gcm_iv[12] __attribute__((aligned(32))) =
{
    0xee, 0xc3, 0x13, 0xdd, 0x07, 0xcc, 0x1b, 0x3e, 0x6b, 0x06, 0x8a, 0x47
};

const uint8_t aes_cbc_iv[16]  __attribute__((aligned(32))) = 
{
	0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x00
};

const unsigned char aes_gcm_aad[20] __attribute__((aligned(32))) =
{
    0x13, 0x89, 0xb5, 0x22,
    0xc2, 0x4a, 0x77, 0x41,
    0x81, 0x70, 0x05, 0x53,
    0xf0, 0x24, 0x6b, 0xba,
    0xbd, 0xd3, 0x8d, 0x6f
};


uint32_t secdata_get_user_data_offset(void)
{
	/* get address from partition table ?? */
	return 0x110000;
}

uint32_t secdata_get_user_dec_key(uint8_t* key)
{
	/* get from secure efuse 2nd key */
	/* 1st key is for secure boot hash */
	return hal_sec_key_get(key, 1, 32);
}

void secdata_dump_mem(char* msg, uint8_t* mem, int mem_len)
{
	char word_buf[32], *curr = word_buf;
	dbg_printf("\n\rDump %s, dump size = %d", msg, mem_len);
	for(int i=0;i<mem_len;i++){
		if( (i&0xF)==0 ){
			*curr = '\0';
			if(i!=0)
				dbg_printf("\t| %s", word_buf);
			dbg_printf("\n\r0x%08x | ", &mem[i]);
			curr = word_buf;
		}
		dbg_printf("%02x ", mem[i]);
		sprintf(curr++, "%c ", (mem[i]<=0x20)?'.':mem[i]);
	}	
	dbg_printf("\t| %s\n\r", word_buf);
}

int secdata_dec_user_data(uint8_t* data, uint32_t offset, uint8_t* key)
{
	int ret = FAIL;
	uint8_t tmpbuf[64];
	uint8_t *dec_tag = (uint8_t*)(((uint32_t)tmpbuf+31)&(~31)); 	// alignto 32byte or using malloc	
	
	uint8_t *flash_mapped_addr = (uint8_t*)(0x98000000+offset);
	
	uint8_t *enc_addr;
	uint8_t *enc_tag;
	
	/* algorithm for decrypting data from flash offset using key */
#if DEMO_ALGORITHM==USE_CBC			
	secdata_dump_mem("CBC IV", (uint8_t*)aes_cbc_iv, 16);
#elif DEMO_ALGORITHM==USE_GCM
	secdata_dump_mem("GCM IV", (uint8_t*)aes_gcm_iv, 12);
#endif
	
	secdata_dump_mem("FLASH DATA", flash_mapped_addr, 256);
	
	
    ret = crypto_init();
    if (SUCCESS != ret) {
        dbg_printf("crypto engine init failed \r\n");
		goto dec_error;
    }

	enc_addr = flash_mapped_addr;
	enc_tag = (uint8_t*)((uint32_t)flash_mapped_addr+SECDATA_SIZE);
	
#if DEMO_ALGORITHM==USE_CBC	
	ret = crypto_aes_cbc_init(key, 32);	// aes 256bit, key length = 256bits/8 = 32 bytes
	if (SUCCESS != ret) {
		dbg_printf("AES CBC init failed, ret = %d \r\n", ret);
		goto dec_error;
	}		
	ret = crypto_aes_cbc_decrypt (enc_addr, SECDATA_SIZE, aes_cbc_iv, 16, data);
	if (SUCCESS != ret) {
		dbg_printf("AES CBC decrypt failed, ret = %d \r\n", ret);
		goto dec_error;
	}
	
	secdata_dump_mem("DECRYPT DATA", data, 256);
	
	return 0;
#elif DEMO_ALGORITHM==USE_GCM
	ret = crypto_aes_gcm_init(key, 32);
	if (SUCCESS != ret) {
		dbg_printf("AES GCM init failed, ret = %d \r\n", ret);
		goto dec_error;
	}
	
	ret = crypto_aes_gcm_decrypt(enc_addr, SECDATA_SIZE, aes_gcm_iv, aes_gcm_aad, sizeof(aes_gcm_aad), data, dec_tag);
	if (SUCCESS != ret) {
		dbg_printf("AES GCM decrypt failed, ret = %d \r\n", ret);
		goto dec_error;
	}		
	
	secdata_dump_mem("DECRYPT DATA", data, 256);
	secdata_dump_mem("DECRYPT TAG", dec_tag, 16);
	
	/* do secdata_data verify */
	if(memcmp(dec_tag, enc_tag, sizeof(enc_tag))==0){
		dbg_printf("Secure Data is valid\n\r");
		return 0;
	}else{
		dbg_printf("Secure Data is invalid\n\r");
		return -1;
	}
#endif
	
dec_error:	
	return -1;
}

sec_data_t* secdata_parse_user_data(uint8_t* data, int *num)
{
	sec_head_t *head = (sec_head_t*)data;
	sec_data_t *data_array;
	uint8_t* data_wo_header;
	
	*num = head->data_num;
	if(*num == 0) return NULL;
	
	data_wo_header = data + (*num+1)*sizeof(int);
	
	data_array = malloc(*num * sizeof(sec_data_t));
	if(!data_array)	return NULL;
	
	int offset = 0;
	for(int i=0;i<*num;i++){
		data_array[i].len = head->data_len_array[i];
		data_array[i].data = data_wo_header + offset;
		offset += data_array[i].len;
	}
	
	return data_array;
}

int secdata_get_data_num(void)
{
	return sec_data_num;
}

uint8_t* secdata_get_data(int index)
{
	if(index >= sec_data_num)
		return NULL;
	
	return sec_data_array[index].data;
}

int secdata_get_data_len(int index)
{
	if(index >= sec_data_num)
		return NULL;
	
	return sec_data_array[index].len;
}

/* Demo usage outside this file */
void secdata_dump_all_data(void)
{
	/* Demo usage outside this file */
	for(int i=0;i<secdata_get_data_num();i++){
		dbg_printf("---------INDEX %d , LEN = %d----------\n\r", i, secdata_get_data_len(i));
		dbg_printf("%s", (char*)secdata_get_data(i));
		dbg_printf("---------INDEX %d --------------------\n\r", i);
	}
}

void NS_ENTRY secdata_load_user_data(void)
{
	uint32_t offset;
	uint8_t tmpbuf[64];
	uint8_t *key = (uint8_t*)(((uint32_t)tmpbuf+31)&(~31)); 	// alignto 32byte or using malloc
		
	dbg_printf(">>>> Load user secure data\n\r");
	
	offset = secdata_get_user_data_offset();
	dbg_printf("From user data flash offset %x\n\r", offset);
	
	secdata_get_user_dec_key(key);
	secdata_dump_mem("AES KEY", key, 32);
	
	if(secdata_dec_user_data(secdata_data, offset, key)==0){
	
		sec_data_array = secdata_parse_user_data(secdata_data, &sec_data_num);
		
	}else{
		sec_data_num = 0;
		sec_data_array = NULL;
	}
	dbg_printf("<<<< Load user secure data\n\r");
	
	secdata_dump_all_data();
}
