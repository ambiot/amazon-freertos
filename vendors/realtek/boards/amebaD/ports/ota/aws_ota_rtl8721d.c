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
#include "rtl8721d_ota.h"
#include "platform_stdlib.h"

extern void PRCMHibernateCycleTrigger(void);
extern uint32_t update_ota_prepare_addr(void);

#define OTA_DEBUG 0
#define OTA_MEMDUMP 0
#define OTA_PRINT DiagPrintf

static uint32_t aws_ota_imgaddr = 0;
static uint32_t aws_ota_imgsz = 0;
static bool_t aws_ota_target_hdr_get = false;
static uint32_t ota_target_index = OTA_INDEX_2;
static uint32_t HdrIdx = 0;
static update_ota_target_hdr aws_ota_target_hdr;
static uint8_t aws_ota_signature[9] = {0};

#define OTA1_FLASH_START_ADDRESS 		LS_IMG2_OTA1_ADDR	//0x08006000
#define OTA1_FLASH_END_ADDRESS 			0x08100000
#define OTA1_FLASH_SIZE		 			(OTA1_FLASH_END_ADDRESS - OTA1_FLASH_START_ADDRESS-1)

#define OTA2_FLASH_START_ADDRESS 		LS_IMG2_OTA2_ADDR	//0x08106000
#define OTA2_FLASH_END_ADDRESS 			0x08200000
#define OTA2_FLASH_SIZE		 			(OTA2_FLASH_END_ADDRESS - OTA2_FLASH_START_ADDRESS-1)

#define AWS_OTA_IMAGE_STATE_FLASH_OFFSET                  ( 0x101000 ) // 0x0810_0000 - 0x0810_2000-1
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW                  0xffffffffU /* 11111111b A new image that hasn't yet been run. */
#define AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT              0xfffffffeU /* 11111110b Image is pending commit and is ready for self test. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID               0xfffffffcU /* 11111100b The image was accepted as valid by the self test code. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID           0xfffffff8U /* 11111000b The image was NOT accepted by the self test code. */

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
       DiagPrintf ("%s", strHeader);

    column = size % 16;
    row = (size / 16) + 1;
    for (index = 0; index < row; index++, line += 16)
    {
        buf = (u8*)line;

        max = (index == row - 1) ? column : 16;
        if ( max==0 ) break; /* If we need not dump this line, break it. */

        DiagPrintf ("\n[%08x] ", line);

        //Hex
        for (index2 = 0; index2 < max; index2++)
        {
            if (index2 == 8)
            DiagPrintf ("  ");
            DiagPrintf ("%02x ", (u8) buf[index2]);
        }

        if (max != 16)
        {
            if (max < 8)
                DiagPrintf ("  ");
            for (index2 = 16 - max; index2 > 0; index2--)
                DiagPrintf ("   ");
        }

    }

    DiagPrintf ("\n");
    return;
}
#endif

static void prvSysReset_rtl8721d(void)
{
	WDG_InitTypeDef WDG_InitStruct;
	u32 CountProcess;
	u32 DivFacProcess;

	//vTaskDelay(100);

	/* CPU reset: Cortex-M3 SCB->AIRCR*/
	//NVIC_SystemReset();

#if defined(CONFIG_MBED_API_EN) && CONFIG_MBED_API_EN
	rtc_backup_timeinfo();
#endif

	WDG_Scalar(10, &CountProcess, &DivFacProcess);
	WDG_InitStruct.CountProcess = CountProcess;
	WDG_InitStruct.DivFacProcess = DivFacProcess;
	WDG_Init(&WDG_InitStruct);

	WDG_Cmd(ENABLE);
}

static __inline__ bool_t prvContextValidate_rtl8721d( OTA_FileContext_t *C )
{
    return pdTRUE;
}

OTA_Err_t prvPAL_Abort_rtl8721d(OTA_FileContext_t *C)
{
    if( NULL != C)
    {
        C->lFileHandle = 0x0;
    }
    return kOTA_Err_None;
}

bool_t prvPAL_CreateFileForRx_rtl8721d(OTA_FileContext_t *C)
{
	int sector_cnt = 0;
        int i=0;
	uint32_t data;
	flash_t flash;

	if (ota_get_cur_index() == OTA_INDEX_1) {
		ota_target_index = OTA_INDEX_2;
		C->lFileHandle = OTA2_FLASH_START_ADDRESS;
		sector_cnt = ((OTA2_FLASH_SIZE - 1)/4096) + 1;
		OTA_PRINT("\n\r[%s] OTA2 address space will be upgraded\n", __FUNCTION__);
	} else {
		ota_target_index = OTA_INDEX_1;
		C->lFileHandle = OTA1_FLASH_START_ADDRESS;
		sector_cnt = ((OTA1_FLASH_SIZE - 1)/4096) + 1;
		OTA_PRINT("\n\r[%s] OTA1 address space will be upgraded\n", __FUNCTION__);
	}

    if (C->lFileHandle > SPI_FLASH_BASE)
    {
    	OTA_PRINT("[OTA] valid ota addr (0x%x) \r\n", C->lFileHandle);
        aws_ota_imgaddr = 0;
        aws_ota_imgsz = 0;
        aws_ota_target_hdr_get = false;
        memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
        memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
        aws_ota_imgaddr = C->lFileHandle;
        data = HAL_READ32(SPI_FLASH_BASE, FLASH_SYSTEM_DATA_ADDR);
        OTA_PRINT("[OTA] addr: 0x%08x, data 0x%08x\r\n", aws_ota_imgaddr, data);
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS || testrunnerFULL_OTA_PAL_ENABLED
#else
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        for( i = 0; i < sector_cnt; i++)
        {
            OTA_PRINT("[OTA] Erase sector @ 0x%x\n", C->lFileHandle - SPI_FLASH_BASE + i * 4096);
            flash_erase_sector(&flash, aws_ota_imgaddr - SPI_FLASH_BASE + i * 4096);
	    }
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
#endif
    }
    else {
        OTA_PRINT("[OTA] invalid ota addr (%d) \r\n", C->lFileHandle);
        C->lFileHandle = NULL;      /* Nullify the file handle in all error cases. */
    }
    return ( C->lFileHandle > SPI_FLASH_BASE ) ? pdTRUE : pdFALSE;
}

/* Read the specified signer certificate from the filesystem into a local buffer. The
 * allocated memory becomes the property of the caller who is responsible for freeing it.
 */
uint8_t * prvPAL_ReadAndAssumeCertificate_rtl8721d(const uint8_t * const pucCertName, int32_t * const lSignerCertSize)
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

static OTA_Err_t prvSignatureVerificationUpdate_rtl8721d(OTA_FileContext_t *C, void * pvContext)
{
    u32 i;
    flash_t flash;
    u8 * pTempbuf;
    int rlen;
    u32 len = aws_ota_imgsz;
    u32 addr = aws_ota_target_hdr.FileImgHdr[HdrIdx].FlashAddr;
    OTA_Err_t eResult = kOTA_Err_None;
    if(len <= 0) {
      eResult = kOTA_Err_SignatureCheckFailed;
      return eResult;
    }

    /*the upgrade space should be masked, because the encrypt firmware is used
    for checksum calculation*/
    //OTF_Mask(1, (addr - SPI_FLASH_BASE), NewImg2BlkSize, 1);

    pTempbuf = ota_update_malloc(BUF_SIZE);
    if(!pTempbuf){
        eResult = kOTA_Err_SignatureCheckFailed;
        goto error;
    }

#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS || testrunnerFULL_OTA_PAL_ENABLED
	/* read flash data back to check signature of the image */
	for(i=0;i<len;i+=BUF_SIZE){
		rlen = (len-i)>BUF_SIZE?BUF_SIZE:(len-i);
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_stream_read(&flash, addr - SPI_FLASH_BASE+i, rlen, pTempbuf);
		Cache_Flush();
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, rlen);
	}
#else
    /*add image signature(81958711)*/
	CRYPTO_SignatureVerificationUpdate(pvContext, aws_ota_signature, 8);

    len = len-8;
    /* read flash data back to check signature of the image */
    for(i=0;i<len;i+=BUF_SIZE){
    	rlen = (len-i)>BUF_SIZE?BUF_SIZE:(len-i);
		device_mutex_lock(RT_DEV_LOCK_FLASH);
    	flash_stream_read(&flash, addr - SPI_FLASH_BASE+i+8, rlen, pTempbuf);
    	Cache_Flush();
    	device_mutex_unlock(RT_DEV_LOCK_FLASH);
		CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, rlen);
    }
#endif

#if 0
    /*mask the rdp flash space to read the rdp image and calculate the checksum*/
    if(aws_ota_target_hdr.RdpStatus == ENABLE) {
#if 0
        for(i = 0; i < aws_ota_target_hdr.FileRdpHdr.ImgLen; i++) {
            flash_stream_read(&flash, RDP_FLASH_ADDR - SPI_FLASH_BASE+i, 1, pTempbuf);
            Cache_Flush();
            CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, 1);
        }
#else
        OTA_PRINT("OTA RDP not implemented yet\r\n");
        configASSERT(0);
        eResult = kOTA_Err_SignatureCheckFailed;
        goto error;
#endif
    }
    OTF_Mask(1, (addr - SPI_FLASH_BASE), NewImg2BlkSize, 0);
#endif

error:
    if(pTempbuf)
        ota_update_free(pTempbuf);
    return eResult;
}

#if (defined(__ICCARM__))
extern void *calloc_freertos(size_t nelements, size_t elementSize);
#else
/*
static void * prvCalloc( size_t xNmemb,
                         size_t xSize )
{
    void * pvNew = pvPortMalloc( xNmemb * xSize );

    if( NULL != pvNew )
    {
        memset( pvNew, 0, xNmemb * xSize );
    }

    return pvNew;
}
*/
#endif
OTA_Err_t prvPAL_SetPlatformImageState_rtl8721d (OTA_ImageState_t eState);
OTA_Err_t prvPAL_CheckFileSignature_rtl8721d(OTA_FileContext_t * const C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CheckFileSignature_rtl8721d" );

    OTA_Err_t eResult;
    int32_t lSignerCertSize;
    void *pvSigVerifyContext;
    uint8_t *pucSignerCert = NULL;
#if (defined(__ICCARM__))
	mbedtls_platform_set_calloc_free(calloc_freertos, vPortFree);
#else
    //mbedtls_platform_set_calloc_free( prvCalloc, vPortFree );
#endif

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
        pucSignerCert = prvPAL_ReadAndAssumeCertificate_rtl8721d((const uint8_t* const)C->pucCertFilepath, &lSignerCertSize);
        if (pucSignerCert == NULL)
        {
            eResult = kOTA_Err_BadSignerCert;
            break;
        }

        eResult = prvSignatureVerificationUpdate_rtl8721d(C, pvSigVerifyContext);
        if(eResult != kOTA_Err_None)
        {
            break;
		}
        if ( CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char*)pucSignerCert, lSignerCertSize,
                                                    C->pxSignature->ucData, C->pxSignature->usSize ) == pdFALSE )
        {
            eResult = kOTA_Err_SignatureCheckFailed;
            prvPAL_SetPlatformImageState_rtl8721d(eOTA_ImageState_Rejected);
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
OTA_Err_t prvPAL_CloseFile_rtl8721d(OTA_FileContext_t *C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile" );
    OTA_Err_t eResult = kOTA_Err_None;

    OTA_PRINT("[OTA] Authenticating and closing file.\r\n");

    if( prvContextValidate_rtl8721d( C ) == pdFALSE )
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
            eResult = prvPAL_CheckFileSignature_rtl8721d( C );
            //eResult = kOTA_Err_None;
        }
        else
        {
            eResult = kOTA_Err_SignatureCheckFailed;
        }
    }
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS || testrunnerFULL_OTA_PAL_ENABLED
	aws_ota_imgaddr = 0;
    aws_ota_imgsz = 0;
    aws_ota_target_hdr_get = false;
    memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
    memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
#endif
    return eResult;
}

int16_t prvPAL_WriteBlock_rtl8721d(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize)
{
    flash_t flash;
    uint32_t address = C->lFileHandle - SPI_FLASH_BASE;
    uint32_t NewImg2Len = 0;
    uint32_t NewImg2BlkSize = 0;
    static bool_t wait_target_img = true;
    static uint32_t img_sign = 0;
    uint32_t WriteLen, offset;
    uint32_t version=0,major=0,minor=0,build=0;

#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS || testrunnerFULL_OTA_PAL_ENABLED
    OTA_PRINT("[OTA_TEST] Write %d bytes @ 0x%x\n", iBlockSize, address+iOffset);
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    FLASH_EraseXIP(EraseSector, C->lFileHandle -SPI_FLASH_BASE);
    if(ota_writestream_user(address+iOffset, iBlockSize, pacData) < 0){
        OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        return -1;
    }
    aws_ota_imgsz += iBlockSize;
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
	memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
	memcpy((void *)aws_ota_signature, pacData, sizeof(aws_ota_signature));
	aws_ota_target_hdr.FileImgHdr[HdrIdx].FlashAddr = C->lFileHandle;
    return iBlockSize;
#endif

	if (aws_ota_target_hdr_get != true)
	{
        u32 RevHdrLen;
        int i;
        if(iOffset == 0)
        {
            //OTA_PRINT("OTA header not found yet\r\n");
            //configASSERT(0);
            //return -1;
	        wait_target_img = true;
	        img_sign = 0;
	        //aws_ota_imgsz = 0;
	        memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
	        memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
	        memcpy((u8*)(&aws_ota_target_hdr.FileHdr), pacData, sizeof(aws_ota_target_hdr.FileHdr));
	        if(aws_ota_target_hdr.FileHdr.HdrNum > 2 || aws_ota_target_hdr.FileHdr.HdrNum <= 0)
	        {
	            OTA_PRINT("INVALID IMAGE BLOCK 0\r\n");
				#if OTA_DEBUG && OTA_MEMDUMP
	            vMemDump(pacData, iBlockSize, NULL);
	            #endif
	            return -1;
	        }
	        memcpy((u8*)(&aws_ota_target_hdr.FileImgHdr[HdrIdx]), pacData+sizeof(aws_ota_target_hdr.FileHdr), 8);
	        RevHdrLen = (aws_ota_target_hdr.FileHdr.HdrNum * aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgHdrLen) + sizeof(aws_ota_target_hdr.FileHdr);
	        if (!get_ota_tartget_header(pacData, RevHdrLen, &aws_ota_target_hdr, ota_target_index))
	        {
	            OTA_PRINT("Get OTA header failed\n");
	            #if OTA_DEBUG && OTA_MEMDUMP
	            vMemDump(pacData, iBlockSize, "Header");
	            #endif
	            return -1;
	        }
#if 0
			printf("aws_ota_target_hdr.FileHdr.FwVer=0x%08X\n",aws_ota_target_hdr.FileHdr.FwVer);
			printf("aws_ota_target_hdr.FileHdr.FwVer=0x%08X\n",aws_ota_target_hdr.FileHdr.HdrNum);
			printf("aws_ota_target_hdr.FileImgHdr[%d].ImgId=%s\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgId);
			printf("aws_ota_target_hdr.FileImgHdr[%d].ImgHdrLen=0x%08X\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgHdrLen);
			printf("aws_ota_target_hdr.FileImgHdr[%d].Checksum=0x%08X\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].Checksum);
			printf("aws_ota_target_hdr.FileImgHdr[%d].ImgLen=0x%08X\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen);
			printf("aws_ota_target_hdr.FileImgHdr[%d].Offset=0x%08X\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].Offset);
			printf("aws_ota_target_hdr.FileImgHdr[%d].FlashAddr=0x%08X\n",HdrIdx, aws_ota_target_hdr.FileImgHdr[HdrIdx].FlashAddr);
			printf("aws_ota_target_hdr.ValidImgCnt=%d\n",aws_ota_target_hdr.ValidImgCnt);
#endif
			version = aws_ota_target_hdr.FileHdr.FwVer;
			major = version / 1000000;
			minor = (version - (major*1000000)) / 1000;
			build = (version - (major*1000000) - (minor * 1000))/1;
			if( aws_ota_target_hdr.FileHdr.FwVer <= (APP_VERSION_MAJOR*1000000 + APP_VERSION_MINOR * 1000 + APP_VERSION_BUILD))
			{
				OTA_PRINT("OTA failed!!!\n");
				OTA_PRINT("New Firmware version(%d,%d,%d) must greater than current firmware version(%d,%d,%d)\n",major,minor,build,APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_BUILD);
				return -1;
			}
			else
			{
				OTA_PRINT("New Firmware version (%d,%d,%d), current firmware version(%d,%d,%d)\n",major,minor,build,APP_VERSION_MAJOR,APP_VERSION_MINOR,APP_VERSION_BUILD);
			}

	        /*get new image length from the firmware header*/
	        NewImg2Len = aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen;
	        NewImg2BlkSize = ((NewImg2Len - 1)/4096) + 1;

	        OTA_PRINT("[OTA][%s] NewImg2BlkSize %d\n", __FUNCTION__, NewImg2BlkSize);
	        //device_mutex_lock(RT_DEV_LOCK_FLASH);
	        //for( i = 0; i < NewImg2BlkSize; i++){
	            //flash_erase_sector(&flash, C->lFileHandle -SPI_FLASH_BASE + i * 4096);
	        //    FLASH_EraseXIP(EraseSector, C->lFileHandle -SPI_FLASH_BASE + i * 4096);
	        //    OTA_PRINT("[OTA] Erase sector @ 0x%x\n", C->lFileHandle - SPI_FLASH_BASE + i * 4096);
	        //}
	        //device_mutex_unlock(RT_DEV_LOCK_FLASH);
	        /*the upgrade space should be masked, because the encrypt firmware is used
	        for checksum calculation*/
	        //OTF_Mask(1, (C->lFileHandle -SPI_FLASH_BASE), NewImg2BlkSize, 1);
	        aws_ota_target_hdr_get = true;
        }
        else
        {
	        aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen = C->ulFileSize - (C->ulFileSize%1024) - 1024;
	        aws_ota_target_hdr.FileHdr.HdrNum = 0x1;
	        aws_ota_target_hdr.FileImgHdr[HdrIdx].Offset = 0x20;
        }
    }

    OTA_PRINT("[OTA][%s] iFileSize %d, iOffset: 0x%x: iBlockSize: 0x%x\n", __FUNCTION__, C->ulFileSize, iOffset, iBlockSize);
	if((C->ulFileSize/OTA_FILE_BLOCK_SIZE) == (iOffset/OTA_FILE_BLOCK_SIZE)){

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

    if(aws_ota_imgsz >= aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen){
        OTA_PRINT("[OTA] image download is already done, dropped, aws_ota_imgsz=0x%X, ImgLen=0x%X\n",aws_ota_imgsz,aws_ota_target_hdr.FileImgHdr[aws_ota_target_hdr.FileHdr.HdrNum].ImgLen);
        return iBlockSize;
    }

    if(wait_target_img == true && iOffset <= aws_ota_target_hdr.FileImgHdr[HdrIdx].Offset) {
        uint32_t byte_to_write = (iOffset + iBlockSize) - aws_ota_target_hdr.FileImgHdr[HdrIdx].Offset;
        int i=0;
        pacData += (iBlockSize -byte_to_write);
        OTA_PRINT("[OTA] FIRST image data arrived %d\n", byte_to_write);
        if(img_sign < 8){
            img_sign = (byte_to_write > 8)?8:byte_to_write;
            memcpy(aws_ota_signature, pacData, img_sign);
            memcpy(aws_ota_target_hdr.Sign[HdrIdx],pacData,img_sign);
            OTA_PRINT("[OTA] Signature get [%d]%s\n", img_sign, aws_ota_signature);
            byte_to_write -= img_sign;
            pacData += img_sign;
            aws_ota_imgsz += img_sign;
        }

        device_mutex_lock(RT_DEV_LOCK_FLASH);
        OTA_PRINT("[OTA] FIRST Write %d bytes @ 0x%x\n", byte_to_write, address);
        //if(flash_stream_write(&flash, address+8, byte_to_write, pacData) < 0){
        if(ota_writestream_user(address+8, byte_to_write, pacData) < 0){
            OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
            return -1;
        }
#if OTA_DEBUG && OTA_MEMDUMP
        vMemDump(pacData, byte_to_write, "PAYLOAD1");
#endif
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        aws_ota_imgsz += byte_to_write;
        if (img_sign >= 8)
            wait_target_img = false;
        return iBlockSize;
    }

    WriteLen = iBlockSize;
    offset = iOffset - aws_ota_target_hdr.FileImgHdr[HdrIdx].Offset;
    if ((offset + iBlockSize) >= aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen) {

		if(offset > aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen)
			return iBlockSize;
        WriteLen -= (offset + iBlockSize - aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen);
        OTA_PRINT("[OTA] LAST image data arrived %d\n", WriteLen);
    }

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    OTA_PRINT("[OTA] Write %d bytes @ 0x%x (0x%x)\n", WriteLen, address + offset, (address + offset - 0x6000));
    if(ota_writestream_user(address + offset, WriteLen, pacData) < 0){
        OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        return -1;
    }
#if OTA_DEBUG && OTA_MEMDUMP
    vMemDump(pacData, iBlockSize, "PAYLOAD2");
#endif
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    aws_ota_imgsz += WriteLen;

    return iBlockSize;
}

OTA_Err_t prvPAL_ActivateNewImage_rtl8721d(void)
{
    flash_t flash;
    /* Reset the MCU so we can test the new image. Short delay for debug log output. */
    OTA_PRINT("[OTA] [%s] Download new firmware %d bytes completed @ 0x%x\n", __FUNCTION__, aws_ota_imgsz, aws_ota_target_hdr.FileImgHdr[HdrIdx].FlashAddr);
    OTA_PRINT("[OTA] signature: %s, size = %d, OtaTargetHdr.FileImgHdr.ImgLen = %d\n", aws_ota_signature, aws_ota_imgsz, aws_ota_target_hdr.FileImgHdr[HdrIdx].ImgLen);
    /*------------- verify checksum and update signature-----------------*/
    if(verify_ota_checksum( &aws_ota_target_hdr)){
        if(!change_ota_signature(&aws_ota_target_hdr, ota_target_index)) {
            OTA_PRINT("[OTA] [%s], change signature failed\r\n", __FUNCTION__);
            return kOTA_Err_ActivateFailed;
        } else {
            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_erase_sector(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET);
            flash_write_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
            OTA_PRINT("[OTA] [%s] Update OTA success!\r\n", __FUNCTION__);
        }
    }else{
        /*if checksum error, clear the signature zone which has been written in flash in case of boot from the wrong firmware*/
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash, aws_ota_target_hdr.FileImgHdr[HdrIdx].FlashAddr - SPI_FLASH_BASE);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        OTA_PRINT("[OTA] [%s] The checksume is wrong!\n\r", __FUNCTION__);
        return kOTA_Err_ActivateFailed;
    }
    OTA_PRINT("[OTA] Resetting MCU to activate new image.\r\n");
    vTaskDelay( 500 );
    prvSysReset_rtl8721d();
    return kOTA_Err_None;
}

OTA_Err_t prvPAL_ResetDevice_rtl8721d ( void )
{
    prvSysReset_rtl8721d();
    return kOTA_Err_None;
}

OTA_Err_t prvPAL_SetPlatformImageState_rtl8721d (OTA_ImageState_t eState)
{
    DEFINE_OTA_METHOD_NAME( "prvSetImageState_rtl8721d" );
    OTA_Err_t eResult = kOTA_Err_Uninitialized;
    uint32_t ota_imagestate =  AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
    flash_t flash;

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &ota_imagestate);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    if ( eState == eOTA_ImageState_Accepted )
    {
        /* This should be an image launched in self test mode! */
        if ( ota_imagestate == AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT )
        {
            /* Mark the image as valid */
            ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID;

            device_mutex_lock(RT_DEV_LOCK_FLASH);
            flash_write_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);

            {
                OTA_LOG_L1( "[%s] Accepted and committed final image.\r\n", OTA_METHOD_NAME );

                /* Disable the watchdog timer. */
                //prvPAL_WatchdogDisable();

                eResult = kOTA_Err_None;
            }
#if 0
            else
            {
                OTA_LOG_L1( "[%s] Accepted final image but commit failed (%d).\r\n", OTA_METHOD_NAME,
                           MCHP_ERR_FLASH_WRITE_FAIL );
                eResult = ( uint32_t ) kOTA_Err_CommitFailed | ( ( ( uint32_t ) MCHP_ERR_FLASH_WRITE_FAIL ) & ( uint32_t ) kOTA_PAL_ErrMask );
            }
#endif
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
	/* The image in the program image bank (upper bank) is rejected so mark it invalid.  */
#if 0
        /* Copy the descriptor in program bank. */
        xDescCopy = *pxAppImgDescProgImageBank;

        /* Mark the image in program bank as invalid */
        xDescCopy.xImgHeader.ucImgFlags = AWS_BOOT_FLAG_IMG_INVALID;

        if ( AWS_NVM_QuadWordWrite( pxAppImgDescProgImageBank->xImgHeader.ulAlign, xDescCopy.xImgHeader.ulAlign,
                                    sizeof( xDescCopy ) / AWS_NVM_QUAD_SIZE ) == ( bool_t ) pdTRUE )
#endif
        {
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS || testrunnerFULL_OTA_PAL_ENABLED
			ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
			device_mutex_lock(RT_DEV_LOCK_FLASH);
    		flash_write_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
    		device_mutex_unlock(RT_DEV_LOCK_FLASH);
            eResult = kOTA_Err_None;
#else
        	u32 ota_target_reset_index;
            OTA_LOG_L1( "[%s] Rejecting and invalidating image.\r\n", OTA_METHOD_NAME );
			if (ota_get_cur_index() == OTA_INDEX_1) {
				ota_target_reset_index = OTA_INDEX_2;
			}
			else {
				ota_target_reset_index = OTA_INDEX_1;
			}
			//device_mutex_lock(RT_DEV_LOCK_FLASH);
			//OTA_Change(ota_target_reset_index);
			//device_mutex_unlock(RT_DEV_LOCK_FLASH);
			//Preventing self timer reset
			ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW;
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			flash_erase_sector(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET);
    		flash_write_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, ota_imagestate);
    		device_mutex_unlock(RT_DEV_LOCK_FLASH);

            eResult = kOTA_Err_None;
#endif
        }
#if 0
        else {
            OTA_LOG_L1( "[%s] Reject failed to invalidate the final image (%d).\r\n", OTA_METHOD_NAME,
                        MCHP_ERR_FLASH_WRITE_FAIL );
            eResult = ( uint32_t ) kOTA_Err_RejectFailed | ( ( ( uint32_t ) MCHP_ERR_FLASH_WRITE_FAIL ) & ( uint32_t ) kOTA_PAL_ErrMask );
        }
#endif
    }
    else if ( eState == eOTA_ImageState_Aborted )
    {
	/* The OTA on program image bank (upper bank) is aborted so mark it as invalid.  */
#if 0
        /* Copy the descriptor in program bank. */
        xDescCopy = *pxAppImgDescProgImageBank;

        /* Mark the image in upper bank as invalid */
        xDescCopy.xImgHeader.ucImgFlags = AWS_BOOT_FLAG_IMG_INVALID;

        if ( AWS_NVM_QuadWordWrite( pxAppImgDescProgImageBank->xImgHeader.ulAlign, xDescCopy.xImgHeader.ulAlign,
                                    sizeof (xDescCopy ) / AWS_NVM_QUAD_SIZE ) == ( bool_t ) pdTRUE  )
#endif
        {
            OTA_LOG_L1( "[%s] Aborting and invalidating image.\r\n", OTA_METHOD_NAME );
            eResult = kOTA_Err_None;
        }
#if 0
        else {
            OTA_LOG_L1( "[%s] Abort failed to invalidate the final image (%d).\r\n", OTA_METHOD_NAME,
                        MCHP_ERR_FLASH_WRITE_FAIL );
            eResult = ( uint32_t ) kOTA_Err_AbortFailed | ( ( ( uint32_t ) MCHP_ERR_FLASH_WRITE_FAIL ) & ( uint32_t ) kOTA_PAL_ErrMask );
        }
#endif
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

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_rtl8721d( void )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState_rtl8721d" );

    //OTA_PRINT("[OTA] %s not implemented yet.\r\n", __func__);
    //BootImageDescriptor_t xDescCopy;
    OTA_PAL_ImageState_t eImageState = eOTA_PAL_ImageState_Unknown;
    //const BootImageDescriptor_t* pxAppImgDesc;
    //pxAppImgDesc = ( const BootImageDescriptor_t* ) KVA0_TO_KVA1( pcFlashLowerBankStart ); /*lint !e923 !e9027 !e9029 !e9033 !e9079 !e9078 !e9087 Please see earlier lint comment header. */
    //xDescCopy = *pxAppImgDesc;
    uint32_t ota_imagestate =  AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
    flash_t flash;

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &ota_imagestate);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    switch ( ota_imagestate )
    {
        case AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT:
        {
            /* Pending Commit means we're in the Self Test phase. */
            printf("ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT\n");
            eImageState = eOTA_PAL_ImageState_PendingCommit;
            break;
        }
        case AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID:
        {
            printf("ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID\n");
            eImageState = eOTA_PAL_ImageState_Valid;
            break;
        }
        case AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW:
        {
            eImageState = eOTA_PAL_ImageState_Valid;
            printf("ota_imagestate = AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW\n");
            break;
        }
        default:
        {
            eImageState = eOTA_PAL_ImageState_Invalid;
            printf("ota_imagestate = default\n");
            break;
        }
    }
    OTA_LOG_L1( "[%s] Image current state (0x%02x)\r\n", OTA_METHOD_NAME, eImageState );
    return eImageState;
}

u8 * prvReadAndAssumeCertificate_rtl8721d(const u8 * const pucCertName, s32 * const lSignerCertSize)
{
    OTA_PRINT( ("prvReadAndAssumeCertificate: not implemented yet\r\n") );
    return NULL;
}

