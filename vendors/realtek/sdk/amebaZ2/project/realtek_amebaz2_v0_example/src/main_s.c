#include "cmsis.h"
#include "rt_printf.h"
#include "diag.h"
#include "crypto_api.h"
#include "platform_stdlib.h"

const char *client_key_s = \
"-----BEGIN EC PARAMETERS-----\r\n" \
"BggqhkjOPQMBBw==\r\n" \
"-----END EC PARAMETERS-----\r\n" \
"-----BEGIN EC PRIVATE KEY-----\r\n" \
"MHcCAQEEIAQxciQmaeuPLUa8VueFj9fTEdqbqLY8jW84NuKtxf+ToAoGCCqGSM49\r\n" \
"AwEHoUQDQgAEtxzt0vQIGEeVZMklv+ZJnkjpSj9IlhZhRyfY4rFyieaD3wo3cnw0\r\n" \
"LJTKAEZCGC7y+4qzkzFY/FVUG+zxwkWVsw==\r\n" \
"-----END EC PRIVATE KEY-----\r\n";

const unsigned char *client_cert_s = \
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

const unsigned char *ca_cert_s = \
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

#if defined(__ICCARM__)
#pragma language = extended
#endif

#undef __crypto_mem_dump
#define __crypto_mem_dump(start,size,prefix) do{ \
dbg_printf(prefix "\r\n"); \
  dump_bytes(start,size); \
}while(0)

// vector from NIST: AES ECB/CBC 256 bits :
// key,IV,AAD,tag start address needs to be 32bytes aligned
const unsigned char aes_256_key[32] __ALIGNED(32) =
{
    0x60, 0x3D, 0xEB, 0x10, 0x15, 0xCA, 0x71, 0xBE,
    0x2B, 0x73, 0xAE, 0xF0, 0x85, 0x7D, 0x77, 0x81,
    0x1F, 0x35, 0x2C, 0x07, 0x3B, 0x61, 0x08, 0xD7,
    0x2D, 0x98, 0x10, 0xA3, 0x09, 0x14, 0xDF, 0xF4
};

const unsigned char aes_iv[16] __ALIGNED(32) =
{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

const unsigned char aes_plaintext[16] =
{
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
};

static u8 cipher_result[1024] __ALIGNED(32);
void secure_api(char* (*a2)(void*), void *a3, int a4)
{
	int ret;
	if(a2 && a3){
		memset(cipher_result, 0, sizeof(cipher_result));
		dbg_printf("AES 256 CBC test Decrypt \r\n");
		ret = crypto_init();
		if (SUCCESS != ret) {
			dbg_printf("crypto engine init failed \r\n");
			goto err;
		}
		ret = crypto_aes_cbc_init(aes_256_key,sizeof(aes_256_key));
		if (SUCCESS != ret) {
			dbg_printf("AES CBC init failed, ret = %d \r\n", ret);
			goto err;
		}
		ret = crypto_aes_cbc_decrypt(a3, a4, aes_iv, sizeof(aes_iv), cipher_result);
		if (SUCCESS != ret) {
			dbg_printf("AES CBC decrypt failed, ret = %d \r\n", ret);
			goto err;
		}
		if (memcmp(&aes_plaintext[0], cipher_result, a4) == 0) {
			dbg_printf("AES 256 decrypt result success \r\n");
			ret = SUCCESS;
		} else {
			dbg_printf("AES 256 decrypt result failed \r\n");
			__crypto_mem_dump((u8 *)&aes_plaintext[0],a4, "sw decrypt msg");
			__crypto_mem_dump(cipher_result, a4, "hw decrypt msg");
			ret = FAIL;
			goto err;
		}
#if defined(__ICCARM__)	
		char *(__cmse_nonsecure_call *func)(void* d);
		func = (char *(__cmse_nonsecure_call *)(void*))a2;
#else
		char * __attribute__((cmse_nonsecure_call)) (*func)(void* d);
		func = cmse_nsfptr_create(a2);           
		if (cmse_is_nsfptr(func)) 
#endif
		{
			rt_printfl("Calling NS API from S world\n\r");
			rt_printfl("Received \"%s\" from NS world\n\r", func(a3));
		}
	}
err:
	return;
}

/**
  * Secure API which can be called from non-secure world
  */
uint32_t NS_ENTRY secure_api_gw (void* a0, int a1, char* (*a2)(void*))
{
	rt_printfl("Calling S API from NS world\n\r");
	secure_api(a2, a0, a1);
	return 0;
}

void NS_ENTRY secure_main (void)
{
	rt_printfl("Start application in S world\n\r");
}
