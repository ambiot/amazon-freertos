#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <cmsis.h>
#include <crypto_api.h>
#include <flash_api.h>

#define USE_CBC 1
#define USE_GCM 2
#define DEMO_ALGORITHM USE_GCM		

#define FORCE_WRITING 1

static char client_key[] =\
"-----BEGIN EC PARAMETERS-----\r\n" \
"BggqhkjOPQMBBw==\r\n" \
"-----END EC PARAMETERS-----\r\n" \
"-----BEGIN EC PRIVATE KEY-----\r\n" \
"MHcCAQEEIAQxciQmaeuPLUa8VueFj9fTEdqbqLY8jW84NuKtxf+ToAoGCCqGSM49\r\n" \
"AwEHoUQDQgAEtxzt0vQIGEeVZMklv+ZJnkjpSj9IlhZhRyfY4rFyieaD3wo3cnw0\r\n" \
"LJTKAEZCGC7y+4qzkzFY/FVUG+zxwkWVsw==\r\n" \
"-----END EC PRIVATE KEY-----\r\n";

static unsigned char client_cert[] =\
"-----BEGIN CERTIFICATE-----\r\n" \
"MIICIDCCAcagAwIBAgIBDzAKBggqhkjOPQQDAjBZMQswCQYDVQQGEwJBVTETMBEG\r\n" \
"A1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0cyBQdHkg\r\n" \
"THRkMRIwEAYDVQQDDAlFQ0RTQV9DQTUwHhcNMTkwMzE4MDkwNzI5WhcNMjAwMzE3\r\n" \
"MDkwNzI5WjBdMQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8G\r\n" \
"A1UECgwYSW50ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMRYwFAYDVQQDDA1FQ0RTQV9D\r\n" \
"TElFTlQ1MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEtxzt0vQIGEeVZMklv+ZJ\r\n" \
"nkjpSj9IlhZhRyfY4rFyieaD3wo3cnw0LJTKAEZCGC7y+4qzkzFY/FVUG+zxwkWV\r\n" \
"s6N7MHkwCQYDVR0TBAIwADAsBglghkgBhvhCAQ0EHxYdT3BlblNTTCBHZW5lcmF0\r\n" \
"ZWQgQ2VydGlmaWNhdGUwHQYDVR0OBBYEFKOsaCZtravcnWFu2jPOgybGniWJMB8G\r\n" \
"A1UdIwQYMBaAFDuPlIvp73SZcqD8gvPLS9xwVei+MAoGCCqGSM49BAMCA0gAMEUC\r\n" \
"IQDmE+cRBvi7xRbr5x0xuzLfou01WX/bfXFKi77Hmc6OOQIgZSJaKiJ8QmmE53JF\r\n" \
"OIyY8szV/j7Rg2qHVPQhwCVJcp0=\r\n" \
"-----END CERTIFICATE-----\r\n";

static unsigned char ca_cert[] =\
"-----BEGIN CERTIFICATE-----\r\n" \
"MIIB+TCCAZ+gAwIBAgIJAIRr3KcObsW7MAoGCCqGSM49BAMCMFkxCzAJBgNVBAYT\r\n" \
"AkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBXaWRn\r\n" \
"aXRzIFB0eSBMdGQxEjAQBgNVBAMMCUVDRFNBX0NBNTAeFw0xOTAzMTgwOTAyNDBa\r\n" \
"Fw0yMDAzMTcwOTAyNDBaMFkxCzAJBgNVBAYTAkFVMRMwEQYDVQQIDApTb21lLVN0\r\n" \
"YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBXaWRnaXRzIFB0eSBMdGQxEjAQBgNVBAMM\r\n" \
"CUVDRFNBX0NBNTBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABGfODWzfkxx+ScTn\r\n" \
"0AL17wjEMcohhwzuaSOJFRoVwV3avuCHtfQcxcVJBslrRjcK/8xeo/prE/JaHyEK\r\n" \
"wYIhYuajUDBOMB0GA1UdDgQWBBQ7j5SL6e90mXKg/ILzy0vccFXovjAfBgNVHSME\r\n" \
"GDAWgBQ7j5SL6e90mXKg/ILzy0vccFXovjAMBgNVHRMEBTADAQH/MAoGCCqGSM49\r\n" \
"BAMCA0gAMEUCIHdctWRECwlVEMj8yEwJTVSRXNWud2jpyuOwKh1bAZ6ZAiEApghQ\r\n" \
"OOcAyflrXv1U0KPsJn7rw78KQoLWfvhf51Qv590=\r\n" \
"-----END CERTIFICATE-----\r\n";

typedef struct sec_data_s{
	uint8_t* data;
	int len;	// string using 0, using strlen to detect string size
}sec_data_t;

typedef struct sec_head_s{
	int data_num;
	int data_len_array[1];
}sec_head_t;

static sec_data_t sec_data_array [] = {
	{(uint8_t*)client_key, 0},
	{(uint8_t*)client_cert, 0},
	{(uint8_t*)ca_cert, 0}
};

void fill_random_number(uint8_t* buf, int size)
{
	srand(time(NULL));
	
	for(int i=0;i<size;i++){
		buf[i] = rand()&0xFF;
	}
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

uint8_t* secdata_generate_data(sec_data_t* array, int *data_len)
{
	// calculate size
	int size = 0;
	int count = 0;
	uint8_t *buf = NULL, *p;

	count = sizeof(sec_data_array)/sizeof(sec_data_t);
	
	sec_head_t* head = malloc( (count+1)*sizeof(int));
	if(!head) goto gen_error;	
	
	head->data_num = 0;
	
	for(int i=0;i<count;i++){
		printf("%s", array[i].data);
		if(array[i].len == 0)
			head->data_len_array[i] = (strlen((char*)array[i].data)+1);	 //include terminate chaxu06rr '\0'
		else
			head->data_len_array[i] = array[i].len;
		
		size += head->data_len_array[i];
		head->data_num++;
	}
	
	*data_len = ((size+4095)&(~4095));
	buf = malloc(*data_len);
	if(!buf) goto gen_error;
	
	fill_random_number(buf, *data_len);
	
	p = buf;
	memcpy(p, head, (count+1)*sizeof(int));
	p += (count+1)*sizeof(int);
	
	for(int i=0;i<count;i++){
		memcpy(p, array[i].data, head->data_len_array[i]);
		p += head->data_len_array[i];
	}
	
	free(head);
	
	dbg_printf("total data length = %d, sec data length = %d\n\r", size, *data_len);
	
	return buf;
	
gen_error:
	if(!buf) free(buf);
	if(!head) free(head);
	return NULL;
}

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

int secdata_encryt_data(uint8_t* output, uint8_t* tag, uint8_t* input, int input_len)
{
	int ret = FAIL;
	// using GCM or CBC 
	secdata_dump_mem("INPUT DATA", input, 128);

	ret = crypto_init();
    if (SUCCESS != ret) {
        dbg_printf("crypto engine init failed \r\n");
		goto enc_error;
    }
	
#if DEMO_ALGORITHM==USE_CBC		
	ret = crypto_aes_cbc_init(aes_256_demo_key, 32);	// aes 256bit, key length = 256bits/8 = 32 bytes
	if (SUCCESS != ret) {
		dbg_printf("AES CBC init failed, ret = %d \r\n", ret);
		goto enc_error;
	}	
	ret = crypto_aes_cbc_encrypt (input, input_len, aes_cbc_iv, 16, output);
	if (SUCCESS != ret) {
		dbg_printf("AES CBC encrypt failed, ret = %d \r\n", ret);
		goto enc_error;
	}
	secdata_dump_mem("OUTPUT DATA", output, 256);
	
#elif DEMO_ALGORITHM==USE_GCM		
	ret = crypto_aes_gcm_init(aes_256_demo_key, 32);
	if (SUCCESS != ret) {
		dbg_printf("AES GCM init failed, ret = %d \r\n", ret);
		goto enc_error;
	}
	
	ret = crypto_aes_gcm_encrypt(input, input_len, aes_gcm_iv, aes_gcm_aad, sizeof(aes_gcm_aad), output, tag);
	if (SUCCESS != ret) {
		dbg_printf("AES GCM encrypt failed, ret = %d \r\n", ret);
		goto enc_error;
	}	
	secdata_dump_mem("OUTPUT DATA", output, 256);
	secdata_dump_mem("OUTPUT TAG", tag, 16);	
	
#endif	
enc_error:
	return ret;
}

void secdata_write_flash(uint32_t offset, uint8_t* buf, int len)
{
	int init_offset = offset;
	flash_t obj;
	
	while(len>0){
		int proc_len = (len>4096)?4096:len;
		flash_erase_sector(&obj, offset);
		flash_stream_write(&obj, offset, proc_len, buf);
		len-=proc_len;
		buf+=proc_len;
		offset+=proc_len;
	}
	
	// dump after writing
	secdata_dump_mem("SECDATA in flash", (uint8_t*)(0x98000000+init_offset), 128);
}

#if DEMO_ALGORITHM==USE_CBC
	#define TAG_LEN 0
#elif DEMO_ALGORITHM==USE_GCM		
	#define TAG_LEN 16
#endif

#define USER_PARTITION_OFFSET 0x110000

int secdata_check_empty(uint32_t offset)
{
	int sum = 0;
	uint8_t *flash_mapped_addr = (uint8_t*)(0x98000000+offset);
	
	for(int i=0;i<4096*2;i++){
		sum += ((~flash_mapped_addr[i])&0xFF);
	}
	
	if(sum==0) return 1;	// empty
	else return 0;			// not empty
}

void secdata_write_demo_cert(void)
{
	int secdata_len;
	uint8_t *secdata_plain;
	uint8_t *secdata_cipher, *secdata_tag; 
	
#if FORCE_WRITING==0
	if(secdata_check_empty(USER_PARTITION_OFFSET)==0){
		dbg_printf("USER Data not empty, skip write demo cert\n\r");
		return;
	}else{
		dbg_printf("USER Data empty, write demo cert\n\r");
	}
#endif

	secdata_plain = secdata_generate_data(sec_data_array, &secdata_len);
	if(!secdata_plain){
		dbg_printf("malloc fail\n\r");
		goto write_error;
	}
	
	secdata_cipher = malloc(secdata_len + TAG_LEN);	// extra space for GCM tag
	if(!secdata_cipher){
		goto write_error;
	}
	secdata_tag = secdata_cipher + secdata_len;
	
	secdata_encryt_data(secdata_cipher, secdata_tag, secdata_plain, secdata_len);
	
	secdata_write_flash(USER_PARTITION_OFFSET, secdata_cipher, secdata_len+TAG_LEN);
	
	// write to flash
write_error:	
	if(!secdata_cipher)	free(secdata_cipher);
	if(!secdata_tag)	free(secdata_tag);
	if(!secdata_plain)	free(secdata_plain);
}
