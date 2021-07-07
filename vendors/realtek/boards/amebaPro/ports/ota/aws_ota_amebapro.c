/*
Amazon FreeRTOS OTA PAL for Realtek Ameba V1.0.0
Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 http://aws.amazon.com/freertos
 http://www.FreeRTOS.org
*/

/* OTA PAL implementation for Realtek Ameba platform. */
#include "aws_iot_ota_pal.h"
#include "aws_iot_ota_agent.h"
#include "iot_crypto.h"
#include "aws_ota_codesigner_certificate.h"
#include "aws_iot_ota_agent_internal.h"
#include "aws_application_version.h"
#include "platform_opts.h"
#if CONFIG_EXAMPLE_AMAZON_AFQP_TESTS
    #include "aws_test_runner_config.h"
#endif
#include "flash_api.h"
#include <device_lock.h>
#include "ota_8195b.h"  /* AmebaPro */
#include "platform_stdlib.h"
#include "hal_flash_boot.h"

//================================================================================

//extern uint32_t sys_update_ota_get_curr_fw_idx(void);
//extern uint32_t sys_update_ota_prepare_addr(void);
extern void sys_disable_fast_boot (void);

//================================================================================

static flash_t flash_ota;

//#define AWS_OTA_IMAGE_HEADER_LEN					4
#define AWS_OTA_IMAGE_SIGNATURE_LEN					32

#define AWS_OTA_IMAGE_STATE_FLASH_OFFSET			0x00008000 // Flash reserved section 0x0000_8000 - 0x0000_9000-1

#define AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW			0xffffffffU /* 11111111b A new image that hasn't yet been run. */
#define AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT		0xfffffffeU /* 11111110b Image is pending commit and is ready for self test. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID			0xfffffffcU /* 11111100b The image was accepted as valid by the self test code. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID		0xfffffff8U /* 11111000b The image was NOT accepted by the self test code. */

//================================================================================
extern uint32_t update_ota_prepare_addr(void);

#define OTA_DEBUG 0
#define OTA_MEMDUMP 0
#define OTA_PRINT printf

uint32_t gNewImgLen=0;

unsigned char sig_backup[32];
static uint32_t aws_ota_imgsz = 0;
static bool_t aws_ota_target_hdr_get = false;


#if OTA_DEBUG && OTA_MEMDUMP
void vMemDump(const u8 *start, u32 size, char * strHeader)
{
    int row, column, index, index2, max;
    u8 *buf, *line;

    if(!start ||(size==0))
            return;

    line = (u8*)start;

    /*
    16 bytes per line
    */
    if (strHeader)
       printf ("%s", strHeader);

    column = size % 16;
    row = (size / 16) + 1;
    for (index = 0; index < row; index++, line += 16)
    {
        buf = (u8*)line;

        max = (index == row - 1) ? column : 16;
        if ( max==0 ) break; /* If we need not dump this line, break it. */

        printf ("\n[%08x] ", line);

        //Hex
        for (index2 = 0; index2 < max; index2++)
        {
            if (index2 == 8)
            printf ("  ");
            printf ("%02x ", (u8) buf[index2]);
        }

        if (max != 16)
        {
            if (max < 8)
                printf ("  ");
            for (index2 = 16 - max; index2 > 0; index2--)
                printf ("   ");
        }

    }

    printf ("\n");
    return;
}
#endif

static void clean_upgrade_region()
{
	uint32_t curr_fw_idx = 0;
        
        fw_img_export_info_type_t *pfw_image_info;
	pfw_image_info = get_fw_img_info_tbl();
	curr_fw_idx = pfw_image_info->loaded_fw_idx;
        
	printf("Current firmware index is fw%d. \r\n",curr_fw_idx);
	if(curr_fw_idx==1)
	{
		printf("Clean invalid firmware index is fw2. \r\n");
		cmd_ota_image(1);  //active : fw1(0) ; invalid : fw2(1)
	}
	else
	{
		printf("Clean invalid firmware index is fw1. \r\n");
		cmd_ota_image(0);  //active : fw2(1) ; invalid : fw1(0)
	}
}

static void prvSysReset_amebaPro(void)
{
	ota_platform_reset();
}

static __inline__ bool_t prvContextValidate_amebaPro( OTA_FileContext_t *C )
{
    return pdTRUE;
}

OTA_Err_t prvPAL_Abort_amebaPro(OTA_FileContext_t *C)
{
    if( NULL != C)
    {
        C->lFileHandle = 0x0;
    }
    return kOTA_Err_None;
}

bool_t prvPAL_CreateFileForRx_amebaPro(OTA_FileContext_t *C)
{
	uint32_t curr_fw_idx = 0;

	fw_img_export_info_type_t *pfw_image_info;
	pfw_image_info = get_fw_img_info_tbl();
	curr_fw_idx = pfw_image_info->loaded_fw_idx;
        
	OTA_LOG_L1("Current firmware index is %d\r\n", curr_fw_idx);

	C->lFileHandle = update_ota_prepare_addr() + SPI_FLASH_BASE;
	if(curr_fw_idx==1)
		OTA_LOG_L1("fw2 address 0x%x will be upgraded\n", C->lFileHandle);
	else
		OTA_LOG_L1("fw1 address 0x%x will be upgraded\n", C->lFileHandle);

	if (C->lFileHandle > SPI_FLASH_BASE)
	{
		aws_ota_imgsz = 0;
		aws_ota_target_hdr_get = false;
	}
	return ( C->lFileHandle > SPI_FLASH_BASE ) ? pdTRUE : pdFALSE;
}

/* Read the specified signer certificate from the filesystem into a local buffer. The
 * allocated memory becomes the property of the caller who is responsible for freeing it.
 */
uint8_t * prvPAL_ReadAndAssumeCertificate_amebaPro(const uint8_t * const pucCertName, int32_t * const lSignerCertSize)
{
	uint8_t*	pucCertData;
	uint32_t	ulCertSize;
	uint8_t 	*pucSignerCert = NULL;

	extern BaseType_t PKCS11_PAL_GetObjectValue( const char * pcFileName,
							   uint8_t ** ppucData,
							   uint32_t * pulDataSize );

	if ( PKCS11_PAL_GetObjectValue( (const char *) pucCertName, &pucCertData, &ulCertSize ) != pdTRUE )
	{	/* Use the back up "codesign_keys.h" file if the signing credentials haven't been saved in the device. */
		pucCertData = (uint8_t*) signingcredentialSIGNING_CERTIFICATE_PEM;
		ulCertSize = sizeof( signingcredentialSIGNING_CERTIFICATE_PEM );
		OTA_LOG_L1( "Assume Cert - No such file: %s. Using header file\r\n",
				   (const char*)pucCertName );
	}
	else
	{
		OTA_LOG_L1( "Assume Cert - file: %s OK\r\n", (const char*)pucCertName );
	}

	/* Allocate memory for the signer certificate plus a terminating zero so we can load it and return to the caller. */
	pucSignerCert = pvPortMalloc( ulCertSize +	1);
	if ( pucSignerCert != NULL )
	{
		memcpy( pucSignerCert, pucCertData, ulCertSize );
		/* The crypto code requires the terminating zero to be part of the length so add 1 to the size. */
		pucSignerCert[ ulCertSize ] = '\0';
		*lSignerCertSize = ulCertSize + 1;
	}
	return pucSignerCert;
}

static OTA_Err_t prvSignatureVerificationUpdate_amebaPro(OTA_FileContext_t *C, void * pvContext)
{
    OTA_Err_t eResult = kOTA_Err_None;
    u32 i;
    int rlen;
    u8 * pTempbuf;
    u32 len = gNewImgLen;
    u32 addr = C->lFileHandle;

    pTempbuf = update_malloc(BUF_SIZE);
    if(!pTempbuf){
        eResult = kOTA_Err_SignatureCheckFailed;
        goto error;
    }

#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
	len = aws_ota_imgsz;
	/* read flash data back to check signature of the image */
	for(i=0;i<len;i+=BUF_SIZE){
		rlen = (len-i)>BUF_SIZE?BUF_SIZE:(len-i);
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_stream_read(&flash_ota, addr - SPI_FLASH_BASE+i, rlen, pTempbuf);
		//Cache_Flush();
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, rlen);
	}
#else
	/*add image signature*/
	CRYPTO_SignatureVerificationUpdate(pvContext, sig_backup, 32);

	len = len-32;
	/* read flash data back to check signature of the image */
	for(i=0;i<len;i+=BUF_SIZE){
		rlen = (len-i)>BUF_SIZE?BUF_SIZE:(len-i);
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_stream_read(&flash_ota, addr - SPI_FLASH_BASE+i+32, rlen, pTempbuf);
		//Cache_Flush();
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, rlen);
	}
#endif
error:
    if(pTempbuf)
        update_free(pTempbuf);

    return eResult;
}

extern void *calloc_freertos(size_t nelements, size_t elementSize);
OTA_Err_t prvPAL_SetPlatformImageState_amebaPro (OTA_ImageState_t eState);
OTA_Err_t prvPAL_CheckFileSignature_amebaPro(OTA_FileContext_t * const C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CheckFileSignature_amebaPro" );

    OTA_Err_t eResult;
    int32_t lSignerCertSize;
    void *pvSigVerifyContext;
    uint8_t *pucSignerCert = NULL;

    //mbedtls_platform_set_calloc_free(calloc_freertos, vPortFree);

    while(true)
    {
        /* Verify an ECDSA-SHA256 signature. */
        if (CRYPTO_SignatureVerificationStart( &pvSigVerifyContext, cryptoASYMMETRIC_ALGORITHM_ECDSA,
                                              cryptoHASH_ALGORITHM_SHA256) == pdFALSE)
        {
            eResult = kOTA_Err_SignatureCheckFailed;
            break;
        }

        OTA_LOG_L1( "[%s] Started %s signature verification, file: %s\r\n", OTA_METHOD_NAME,
                   cOTA_JSON_FileSignatureKey, (const char*)C->pucCertFilepath);
        pucSignerCert = prvPAL_ReadAndAssumeCertificate_amebaPro((const uint8_t* const)C->pucCertFilepath, &lSignerCertSize);
        if (pucSignerCert == NULL)
        {
            eResult = kOTA_Err_BadSignerCert;
            break;
        }

        eResult = prvSignatureVerificationUpdate_amebaPro(C, pvSigVerifyContext);
        if(eResult != kOTA_Err_None)
        {
            break;
		}
        if ( CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char*)pucSignerCert, lSignerCertSize,
                                                    C->pxSignature->ucData, C->pxSignature->usSize ) == pdFALSE )
        {
            eResult = kOTA_Err_SignatureCheckFailed;
            prvPAL_SetPlatformImageState_amebaPro(eOTA_ImageState_Rejected);
        }
        else
        {
            eResult = kOTA_Err_None;
        }
        break;
    }
    /* Free the signer certificate that we now own after prvPAL_ReadAndAssumeCertificate(). */
    if( pucSignerCert != NULL )
    {
        vPortFree(pucSignerCert);
    }
    return eResult;
}

/* Close the specified file. This will also authenticate the file if it is marked as secure. */
OTA_Err_t prvPAL_CloseFile_amebaPro(OTA_FileContext_t *C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile" );
    OTA_Err_t eResult = kOTA_Err_None;

    OTA_PRINT("[OTA] Authenticating and closing file.\r\n");

    if( prvContextValidate_amebaPro( C ) == pdFALSE )
    {
        eResult = kOTA_Err_FileClose;
    }

    if( kOTA_Err_None == eResult )
    {
        if( C->pxSignature != NULL )
        {
            OTA_LOG_L1( "[%s] Authenticating and closing file.\r\n", OTA_METHOD_NAME );
            #if OTA_DEBUG && OTA_MEMDUMP
            vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "Signature");
            #endif
            /* TODO: Verify the file signature, close the file and return the signature verification result. */
            eResult = prvPAL_CheckFileSignature_amebaPro( C );
            //eResult = kOTA_Err_None;
        }
        else
        {
            eResult = kOTA_Err_SignatureCheckFailed;
        }
    }
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
    aws_ota_imgsz = 0;
    aws_ota_target_hdr_get = false;
#endif
    return eResult;
}

int16_t prvPAL_WriteBlock_amebaPro(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize)
{
    static bool_t wait_target_img = true;
    static uint32_t img_sign = 0;

	uint32_t address = C->lFileHandle - SPI_FLASH_BASE;
    uint32_t NewImg2Len = 0;
    uint32_t NewImg2BlkSize = 0;
    uint32_t WriteLen, offset;
	uint32_t NewFWAddr = 0;

#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
		OTA_PRINT("[OTA_TEST] Write %d bytes @ 0x%x\n", iBlockSize, address+iOffset);
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_erase_sector(&flash_ota, C->lFileHandle - SPI_FLASH_BASE);
		if(flash_stream_write(&flash_ota, address+iOffset, iBlockSize, pacData) < 0){
			OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
			return -1;
		}
		aws_ota_imgsz += iBlockSize;
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		return iBlockSize;
#endif

	gNewImgLen = C->ulFileSize - (C->ulFileSize%1024);
	if(gNewImgLen<1024)
	{
		OTA_PRINT("Invalid file size:%d\n",gNewImgLen);
		return -1;
	}

	if (aws_ota_target_hdr_get != true) {
		int i;
		//if(iOffset != 0){
		//	OTA_PRINT("OTA header not found yet\r\n");
		//	return -1;
		//}
		wait_target_img = true;
		img_sign = 0;

		NewFWAddr = update_ota_prepare_addr();
		/*get new image length from the firmware header*/
		NewImg2Len = update_ota_erase_upg_region(gNewImgLen, NewImg2Len, NewFWAddr);
		NewImg2BlkSize = ((NewImg2Len - 1)/4096) + 1;

		OTA_PRINT("[OTA][%s] NewImg2BlkSize %d\n", __FUNCTION__, NewImg2BlkSize);
		//device_mutex_lock(RT_DEV_LOCK_FLASH);
		//for( i = 0; i < NewImg2BlkSize; i++){
		//	flash_erase_sector(&flash_ota, C->lFileHandle - SPI_FLASH_BASE + i * 4096);
		//	OTA_PRINT("[OTA] Erase sector @ 0x%x\n", C->lFileHandle - SPI_FLASH_BASE + i * 4096);
		//}
		//device_mutex_unlock(RT_DEV_LOCK_FLASH);
		aws_ota_target_hdr_get = true;
	}

	OTA_PRINT("[OTA][%s] iFileSize %d, iOffset: 0x%x: iBlockSize: 0x%x\n", __FUNCTION__, C->ulFileSize, iOffset, iBlockSize);
	if((C->ulFileSize/OTA_FILE_BLOCK_SIZE) == (iOffset/OTA_FILE_BLOCK_SIZE))
	{
		if(C->pxSignature != NULL && C->pxSignature->usSize > 10 )
		{
			OTA_PRINT("[OTA][%s] OTA1 Sig Size is %d\n", __FUNCTION__, C->pxSignature->usSize);
			#if OTA_DEBUG && OTA_MEMDUMP
			vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "AWS_Signature");
			#endif
		}
		else
		{
			OTA_PRINT("[OTA] Final block with signature arrived\n");
			#if OTA_DEBUG && OTA_MEMDUMP
			vMemDump(pacData, iBlockSize, "Full signature");
			#endif
			uint32_t ota_size = iBlockSize-1;
			OTA_PRINT("[OTA][%s] OTA1 Sig Size is %d\n", __FUNCTION__, ota_size);
			C->pxSignature->usSize = ota_size;
			memcpy(C->pxSignature->ucData, pacData, ota_size);
			#if OTA_DEBUG && OTA_MEMDUMP
			vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "Signature Write");
			#endif
		}
	}

	if(aws_ota_imgsz >= gNewImgLen){
		OTA_PRINT("[OTA] image download is already done, dropped, aws_ota_imgsz=0x%X, ImgLen=0x%X\n",aws_ota_imgsz,gNewImgLen);
		return iBlockSize;
	}

	if(wait_target_img == true && iOffset <= (AWS_OTA_IMAGE_SIGNATURE_LEN)) {
		uint32_t byte_to_write = (iOffset + iBlockSize);
#if OTA_DEBUG && OTA_MEMDUMP
		vMemDump(pacData, iBlockSize, "PAYLOAD0");
#endif
		pacData += (iBlockSize - byte_to_write);
		OTA_PRINT("[OTA] FIRST image data arrived %d\n", byte_to_write);
        if(img_sign < 32){
            img_sign = (byte_to_write > 32)?32:byte_to_write;
            memcpy(sig_backup, pacData, img_sign);
            OTA_PRINT("[OTA] Signature get [%d]\n", img_sign);
            byte_to_write -= img_sign;
            pacData += img_sign;
            aws_ota_imgsz += img_sign;
        }

		device_mutex_lock(RT_DEV_LOCK_FLASH);
		OTA_PRINT("[OTA] FIRST Write %d bytes @ 0x%x\n", byte_to_write, address);
		if(flash_stream_write(&flash_ota, address+AWS_OTA_IMAGE_SIGNATURE_LEN, byte_to_write, pacData) < 0){
			OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
			return -1;
		}
#if OTA_DEBUG && OTA_MEMDUMP
		vMemDump(pacData, byte_to_write, "PAYLOAD1");
#endif
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		aws_ota_imgsz += byte_to_write;
		wait_target_img = false;
		return iBlockSize;
	}

	WriteLen = iBlockSize;
	offset = iOffset;
	if ((offset + iBlockSize) >= gNewImgLen) {

		if(offset > gNewImgLen)
			return iBlockSize;
		WriteLen -= (offset + iBlockSize - gNewImgLen);
		OTA_PRINT("[OTA] LAST image data arrived %d\n", WriteLen);
	}

	device_mutex_lock(RT_DEV_LOCK_FLASH);
	OTA_PRINT("[OTA] Write %d bytes @ 0x%x (0x%x)\n", WriteLen, address + offset, (address + offset));
	if(flash_stream_write(&flash_ota, address + offset, WriteLen, pacData) < 0){
		OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		return -1;
	}

	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	aws_ota_imgsz += WriteLen;
	return iBlockSize;
}

OTA_Err_t prvPAL_ActivateNewImage_amebaPro(void)
{
	int ret = -1;
	uint32_t NewFWAddr = 0;

	NewFWAddr = update_ota_prepare_addr();
	if(NewFWAddr == -1){
		return kOTA_Err_ActivateFailed;
	}

	ret = update_ota_signature(sig_backup, NewFWAddr);
	if(ret == -1){
		OTA_PRINT("[%s] Update signature fail\r\n", __FUNCTION__);
		return kOTA_Err_ActivateFailed;
	}
	else{
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_erase_sector(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET);
		flash_write_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT);
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		OTA_PRINT("[OTA] [%s] Update OTA success!\r\n", __FUNCTION__);
	}

	OTA_PRINT("[OTA] Resetting MCU to activate new image.\r\n");
	vTaskDelay( 100 );
	prvSysReset_amebaPro();
	return kOTA_Err_None;
}

OTA_Err_t prvPAL_ResetDevice_amebaPro (void)
{
    prvSysReset_amebaPro();
    return kOTA_Err_None;
}

OTA_Err_t prvPAL_SetPlatformImageState_amebaPro (OTA_ImageState_t eState)
{
    DEFINE_OTA_METHOD_NAME( "prvSetImageState_amebaPro" );
    OTA_Err_t eResult = kOTA_Err_Uninitialized;
    uint32_t ota_imagestate =  AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &ota_imagestate);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    if ( eState == eOTA_ImageState_Accepted )
    {
        /* This should be an image launched in self test mode! */
        if ( ota_imagestate == AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT )
        {
            /* Mark the image as valid */
            ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID;

            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_write_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);

            {
                OTA_LOG_L1( "[%s] Accepted and committed final image.\r\n", OTA_METHOD_NAME );
                update_ota_prepare_addr();
                eResult = kOTA_Err_None;
            }
        }
        else
        {
            OTA_LOG_L1( "[%s] Warning: image is not pending commit (0x%02x)\r\n", OTA_METHOD_NAME, ota_imagestate );
            if ( ota_imagestate == AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID )
            {
                eResult = kOTA_Err_None;
            }
            else
            {
                eResult = ( uint32_t ) kOTA_Err_CommitFailed/* | ( ( ( uint32_t ) MCHP_ERR_NOT_PENDING_COMMIT ) & ( uint32_t ) kOTA_PAL_ErrMask )*/;
            }
        }
    }
    else if ( eState == eOTA_ImageState_Rejected )
    {
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
            ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_write_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
            eResult = kOTA_Err_None;
#else
            u32 ota_target_reset_index;
            OTA_LOG_L1( "[%s] Rejecting and invalidating image.\r\n", OTA_METHOD_NAME );
            //clean_upgrade_region();
            //ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW;
            ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_erase_sector(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET);
            flash_write_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);

            eResult = kOTA_Err_None;
#endif
    }
    else if ( eState == eOTA_ImageState_Aborted )
    {
        OTA_LOG_L1( "[%s] Aborting and invalidating image.\r\n", OTA_METHOD_NAME );
        eResult = kOTA_Err_None;
    }
    else if ( eState == eOTA_ImageState_Testing )
    {
        OTA_LOG_L1( "[%s] Testing image.\r\n", OTA_METHOD_NAME );
        eResult = kOTA_Err_None;
    }
    else
    {
        OTA_LOG_L1( "[%s] Bad Image State.\r\n", OTA_METHOD_NAME );
        eResult = kOTA_Err_BadImageState;
    }
	return eResult;
}

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_amebaPro( void )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState_amebaPro" );
    OTA_PAL_ImageState_t eImageState = eOTA_PAL_ImageState_Unknown;

    uint32_t ota_imagestate =  AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &ota_imagestate);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    switch ( ota_imagestate )
    {
        case AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT:
        {
            /* Pending Commit means we're in the Self Test phase. */
            eImageState = eOTA_PAL_ImageState_PendingCommit;
            break;
        }
        case AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID:
        {
            eImageState = eOTA_PAL_ImageState_Valid;
            break;
        }
        case AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW:
        {
            eImageState = eOTA_PAL_ImageState_Valid;
            break;
        }
        default:
        {
            eImageState = eOTA_PAL_ImageState_Invalid;
            break;
        }
    }
    OTA_LOG_L1( "[%s] Image current state (0x%02x)\r\n", OTA_METHOD_NAME, eImageState );
    return eImageState;
}

u8 * prvReadAndAssumeCertificate_amebaPro(const u8 * const pucCertName, s32 * const lSignerCertSize)
{
    OTA_PRINT( ("prvReadAndAssumeCertificate: not implemented yet\r\n") );
    return NULL;
}

