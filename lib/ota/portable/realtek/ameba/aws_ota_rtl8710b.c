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
#include "aws_ota_pal.h"
#include "aws_ota_agent.h"
#include "aws_crypto.h"
#include "aws_ota_codesigner_certificate.h"
#include "Aws_ota_agent_internal.h"
#include "platform_opts.h"
#if CONFIG_EXAMPLE_AMAZON_AFQP_TESTS
#include "aws_test_runner_config.h"
#endif
#include "flash_api.h"
#include <device_lock.h>
#include "rtl8710b_ota.h"
#include "platform_stdlib.h"

extern void PRCMHibernateCycleTrigger(void);
extern uint32_t http_ota_target_index;
extern const update_file_img_id OtaImgId[2];
extern uint32_t update_ota_prepare_addr(void);

#define OTA_DEBUG 0
#define OTA_MEMDUMP 0
#define OTA_PRINT DiagPrintf

static uint32_t aws_ota_imgaddr = 0;
static uint32_t aws_ota_imgsz = 0;
static bool_t aws_ota_target_hdr_get = false;
static update_ota_target_hdr aws_ota_target_hdr;
static uint8_t aws_ota_signature[9] = {0};
#define AWS_OTA_IMAGE_STATE_FLASH_OFFSET                  ( 0xFC000 )
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_NEW                  0xffffffffU /* 11111111b A new image that hasn't yet been run. */
#define AWS_OTA_IMAGE_STATE_FLAG_PENDING_COMMIT              0xfffffffeU /* 11111110b Image is pending commit and is ready for self test. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_VALID               0xfffffffcU /* 11111100b The image was accepted as valid by the self test code. */
#define AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID           0xfffffff8U /* 11111000b The image was NOT accepted by the self test code. */

#if 1//OTA_DEBUG && OTA_MEMDUMP
static void vMemDump(const u8 *start, u32 size, char * strHeader)
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

static void prvSysReset_rtl8710b(void)
{
    CPU_ClkSet(CLK_31_25M);
    vTaskDelay(100);
    /* CPU reset: Cortex-M3 SCB->AIRCR*/
    NVIC_SystemReset();	
}

static __inline__ bool_t prvContextValidate_rtl8710b( OTA_FileContext_t *C )
{
    return pdTRUE;
}

OTA_Err_t prvPAL_Abort_rtl8710b(OTA_FileContext_t *C)
{
    if( NULL != C)
    {
        C->lFileHandle = 0x0;        
    }
    return kOTA_Err_None;
}

bool_t prvPAL_CreateFileForRx_rtl8710b(OTA_FileContext_t *C)
{
    uint32_t data;
	int i = 0;
	flash_t flash;
    C->lFileHandle = update_ota_prepare_addr();
    if (C->lFileHandle > SPI_FLASH_BASE)
    {
        aws_ota_imgaddr = 0;
        aws_ota_imgsz = 0;
        aws_ota_target_hdr_get = false;
        memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
        memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
        aws_ota_imgaddr = C->lFileHandle;
        data = HAL_READ32(SPI_FLASH_BASE, OFFSET_DATA);
        OTA_PRINT("[OTA] addr: 0x%08x, data 0x%08x\r\n", aws_ota_imgaddr, data);
	device_mutex_lock(RT_DEV_LOCK_FLASH);
            for( i = 0; i < (((OTA2_ADDR-OTA1_ADDR-1)/4096)+1); i++){
                flash_erase_sector(&flash, aws_ota_imgaddr - SPI_FLASH_BASE + i * 4096);
	    }
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
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
uint8_t * prvPAL_ReadAndAssumeCertificate(const uint8_t * const pucCertName, int32_t * const lSignerCertSize)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ReadAndAssumeCertificate" );

    uint8_t*    pucCertData;
    uint32_t    ulCertSize;
    uint8_t		*pucSignerCert = NULL;
    
    extern BaseType_t PKCS11_PAL_GetObjectValue( const char * pcFileName,
                               uint8_t ** ppucData,
                               uint32_t * pulDataSize );

    if ( PKCS11_PAL_GetObjectValue( (const char *) pucCertName, &pucCertData, &ulCertSize ) != pdTRUE )
    {   /* Use the back up "codesign_keys.h" file if the signing credentials haven't been saved in the device. */
        pucCertData = (uint8_t*) signingcredentialSIGNING_CERTIFICATE_PEM;
        ulCertSize = sizeof( signingcredentialSIGNING_CERTIFICATE_PEM );
        OTA_LOG_L1( "[%s] Assume Cert - No such file: %s. Using header file\r\n", OTA_METHOD_NAME,
                   (const char*)pucCertName );
    }
    else
    {
        OTA_LOG_L1( "[%s] Assume Cert - file: %s OK\r\n", OTA_METHOD_NAME, (const char*)pucCertName );
    }

    /* Allocate memory for the signer certificate plus a terminating zero so we can load it and return to the caller. */
    pucSignerCert = pvPortMalloc( ulCertSize +  1);
    if ( pucSignerCert != NULL )
    {
        memcpy( pucSignerCert, pucCertData, ulCertSize );
        /* The crypto code requires the terminating zero to be part of the length so add 1 to the size. */
        pucSignerCert[ ulCertSize ] = '\0';
        *lSignerCertSize = ulCertSize + 1;
    }
    return (uint8_t *)pucSignerCert;
}

static OTA_Err_t prvSignatureVerificationUpdate_rtl8710b(OTA_FileContext_t *C, void * pvContext)
{
    u32 i;
    flash_t flash;
    u32 flash_checksum=0;
    u32 rdp_checksum=0;
    u32 NewImg2BlkSize;
    u8 * pTempbuf;
    int k;
    int rlen;
    u32 len = aws_ota_imgsz;
    u32 addr = aws_ota_target_hdr.FileImgHdr.FlashOffset;
    OTA_Err_t eResult = kOTA_Err_None;
    if(len <= 0) {
      eResult = kOTA_Err_SignatureCheckFailed;
      return eResult;
    }
    NewImg2BlkSize = ((len - 1)/4096) + 1;

    /*the upgrade space should be masked, because the encrypt firmware is used 
    for checksum calculation*/
    OTF_Mask(1, (addr - SPI_FLASH_BASE), NewImg2BlkSize, 1);

    pTempbuf = ota_update_malloc(BUF_SIZE);
    if(!pTempbuf){
        eResult = kOTA_Err_SignatureCheckFailed;
        goto error;
    }
    /*add image signature*/
   CRYPTO_SignatureVerificationUpdate(pvContext, aws_ota_signature, 8);

    len = len-8;
    /* read flash data back to check signature of the image */
    for(i=0;i<len;i+=BUF_SIZE){
    	rlen = (len-i)>BUF_SIZE?BUF_SIZE:(len-i);
    	flash_stream_read(&flash, addr - SPI_FLASH_BASE+i+8, rlen, pTempbuf);
    	Cache_Flush();
       CRYPTO_SignatureVerificationUpdate(pvContext, pTempbuf, rlen);
    }

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

error:
    if(pTempbuf)
        ota_update_free(pTempbuf);
    return eResult;	
}

extern void *calloc_freertos(size_t nelements, size_t elementSize);
OTA_Err_t prvPAL_SetPlatformImageState_rtl8710b (OTA_ImageState_t eState);
OTA_Err_t prvPAL_CheckFileSignature(OTA_FileContext_t * const C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CheckFileSignature_rtl8710b" );

    OTA_Err_t eResult;
    int32_t lSignerCertSize;
    void *pvSigVerifyContext;
    uint8_t *pucSignerCert = NULL;
    mbedtls_platform_set_calloc_free(calloc_freertos, vPortFree);

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
        pucSignerCert = prvPAL_ReadAndAssumeCertificate((const uint8_t* const)C->pucCertFilepath, &lSignerCertSize);
        if (pucSignerCert == NULL)
        {
            eResult = kOTA_Err_BadSignerCert;
            break;
        }

        eResult = prvSignatureVerificationUpdate_rtl8710b(C, pvSigVerifyContext);
        if(eResult != kOTA_Err_None)
            break;

        if ( CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char*)pucSignerCert, lSignerCertSize,
                                                    C->pxSignature->ucData, C->pxSignature->usSize ) == pdFALSE )
        {
            eResult = kOTA_Err_SignatureCheckFailed;
            prvPAL_SetPlatformImageState_rtl8710b(eOTA_ImageState_Rejected);
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
OTA_Err_t prvPAL_CloseFile_rtl8710b(OTA_FileContext_t *C)
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile" );
    OTA_Err_t eResult = kOTA_Err_None;

    OTA_PRINT("[OTA] Authenticating and closing file.\r\n");

    if( prvContextValidate_rtl8710b( C ) == pdFALSE )
    {
        eResult = kOTA_Err_FileClose;
    }

    if( kOTA_Err_None == eResult )
    {
        if( C->pxSignature != NULL )
        {
            OTA_LOG_L1( "[%s] Authenticating and closing file.\r\n", OTA_METHOD_NAME );
            vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "Signature");
            /* TODO: Verify the file signature, close the file and return the signature verification result. */
            eResult = prvPAL_CheckFileSignature( C );
            //eResult = kOTA_Err_None;
        }
        else
        {
            eResult = kOTA_Err_SignatureCheckFailed;
        }
    }
#if testrunnerFULL_OTA_PAL_ENABLED
	aws_ota_imgaddr = 0;
    aws_ota_imgsz = 0;
    aws_ota_target_hdr_get = false;
    memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
    memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
#endif
    return eResult;
}

int16_t prvPAL_WriteBlock_rtl8710b(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize)
{
    flash_t flash;
    uint32_t address = C->lFileHandle - SPI_FLASH_BASE;
    uint32_t NewImg2Len = 0;
    uint32_t NewImg2BlkSize = 0;
    static bool_t wait_target_img = true;
    static uint32_t img_sign = 0;
    uint32_t WriteLen, offset;
    
#if testrunnerFULL_OTA_PAL_ENABLED
    OTA_PRINT("[OTA_TEST] Write %d bytes @ 0x%x\n", iBlockSize, address+iOffset);
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    if(flash_stream_write(&flash, address+iOffset, iBlockSize, pacData) < 0){
        OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        return -1;
    }
    aws_ota_imgsz += iBlockSize;
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
	memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
	memcpy((void *)aws_ota_signature, pacData, sizeof(aws_ota_signature));
	aws_ota_target_hdr.FileImgHdr.FlashOffset = C->lFileHandle;
    return iBlockSize;
#endif

    if (aws_ota_target_hdr_get != true) {
        u8 ImgId[5] = {0};
        u32 RevHdrLen;
        int i;
        if(iOffset != 0){
            OTA_PRINT("OTA header not found yet\r\n");
            //configASSERT(0);
            return -1;
        }
        wait_target_img = true;
        img_sign = 0;
        aws_ota_imgaddr = 0;
        aws_ota_imgsz = 0;
        memset((void *)&aws_ota_target_hdr, 0, sizeof(update_ota_target_hdr));
        memset((void *)aws_ota_signature, 0, sizeof(aws_ota_signature));
        memcpy(ImgId, (u8 *)(&OtaImgId[http_ota_target_index]), 4);
        memcpy((u8*)(&aws_ota_target_hdr.FileHdr), pacData, sizeof(aws_ota_target_hdr.FileHdr));
        if(aws_ota_target_hdr.FileHdr.HdrNum > 2 || aws_ota_target_hdr.FileHdr.HdrNum <= 0) {
            OTA_PRINT("INVALID IMAGE BLOCK 0\r\n");
            vMemDump(pacData, iBlockSize, NULL);
            vTaskDelay(500);
            configASSERT(0);
            return -1;
        }
        memcpy((u8*)(&aws_ota_target_hdr.FileImgHdr), pacData+sizeof(aws_ota_target_hdr.FileHdr), 8);
        RevHdrLen = (aws_ota_target_hdr.FileHdr.HdrNum * aws_ota_target_hdr.FileImgHdr.ImgHdrLen) + sizeof(aws_ota_target_hdr.FileHdr);
        OTA_PRINT("TempBuf = %s\n", ImgId);
        if (!get_ota_tartget_header(pacData, RevHdrLen, &aws_ota_target_hdr, ImgId)) {
            OTA_PRINT("Get OTA header failed\n");
            return -1;
        }
        /*get new image length from the firmware header*/
        NewImg2Len = aws_ota_target_hdr.FileImgHdr.ImgLen;
        OTA_PRINT("New image length = %d\n", NewImg2Len);
        NewImg2BlkSize = ((NewImg2Len - 1)/4096) + 1;
        /* if OTA1 will be update, image size should not cross OTA2 */
        if(http_ota_target_index== OTA_INDEX_1) {
            if(aws_ota_target_hdr.FileImgHdr.ImgLen > (OTA2_ADDR - OTA1_ADDR)) {
                OTA_PRINT("[OTA][%s] illegal new image length 0x%x\r\n", __FUNCTION__, aws_ota_target_hdr.FileImgHdr.ImgLen);
                return -1;
            }
        }
        OTA_PRINT("[OTA][%s] NewImg2BlkSize %d\n", __FUNCTION__, NewImg2BlkSize);
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        for( i = 0; i < NewImg2BlkSize; i++){
            flash_erase_sector(&flash, C->lFileHandle -SPI_FLASH_BASE + i * 4096);
            OTA_PRINT("[OTA] Erase sector @ 0x%x\n", C->lFileHandle -SPI_FLASH_BASE + i * 4096);
        }
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        /*the upgrade space should be masked, because the encrypt firmware is used 
        for checksum calculation*/
        OTF_Mask(1, (C->lFileHandle -SPI_FLASH_BASE), NewImg2BlkSize, 1);
        aws_ota_target_hdr_get = true;
    }
    OTA_PRINT("[OTA][%s] iFileSize %d, iOffset: 0x%x: iBlockSize: 0x%x\n", __FUNCTION__, C->ulFileSize, iOffset, iBlockSize);
	if((C->ulFileSize/OTA_FILE_BLOCK_SIZE) == (iOffset/OTA_FILE_BLOCK_SIZE)){
		OTA_PRINT("[OTA] Final block with signature arrived\n");
		vMemDump(pacData, iBlockSize, "Full signature");
		uint32_t ota1_size = *(pacData+(iBlockSize-2));
		uint32_t ota2_size = *(pacData+(iBlockSize-1));
		if(http_ota_target_index == OTA_INDEX_1) {
			OTA_PRINT("[OTA][%s] OTA1 Sig Size is %d\n", __FUNCTION__, ota1_size);
			C->pxSignature->usSize = ota1_size;
			memcpy(C->pxSignature->ucData, pacData, ota1_size);
			vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "Signature Write");
			}
		else {
			OTA_PRINT("[OTA][%s] OTA2 Sig Size is %d\n", __FUNCTION__, ota2_size);
			C->pxSignature->usSize = ota2_size;
			memcpy(C->pxSignature->ucData, pacData+ota1_size, ota2_size);
			vMemDump(C->pxSignature->ucData, C->pxSignature->usSize, "Signature Write");
			}
		}
    if(((iOffset + iBlockSize) <= aws_ota_target_hdr.FileImgHdr.Offset) || (iOffset >= (aws_ota_target_hdr.FileImgHdr.Offset + aws_ota_target_hdr.FileImgHdr.ImgLen))) {
        OTA_PRINT("[OTA] It's not OTA%d, skipped\n", http_ota_target_index+1);
        return iBlockSize;
    }
    if(aws_ota_imgsz >= aws_ota_target_hdr.FileImgHdr.ImgLen){
        OTA_PRINT("[OTA] image download is already done, dropped\n");
        return iBlockSize;
    }
    if(wait_target_img == true && iOffset <= aws_ota_target_hdr.FileImgHdr.Offset) {
        uint32_t byte_to_write = (iOffset + iBlockSize) - aws_ota_target_hdr.FileImgHdr.Offset;
        pacData += (iBlockSize -byte_to_write);
        OTA_PRINT("[OTA] FIRST image data arrived %d\n", byte_to_write);
        if(img_sign < 8){
            img_sign = (byte_to_write > 8)?8:byte_to_write;
            memcpy(aws_ota_signature, pacData, img_sign);
            OTA_PRINT("[OTA] Signature get [%d]%s\n", img_sign, aws_ota_signature);
            byte_to_write -= img_sign;
            pacData += img_sign;
            aws_ota_imgsz += img_sign;
        }
        OTA_PRINT("[OTA] FIRST Write %d bytes @ 0x%x\n", byte_to_write, address);
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        if(flash_stream_write(&flash, address+8, byte_to_write, pacData) < 0){
            OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
            device_mutex_unlock(RT_DEV_LOCK_FLASH);
            return -1;
        }
#if OTA_DEBUG && OTA_MEMDUMP
        vMemDump(pacData, byte_to_write, "PAYLOAD");
#endif
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        aws_ota_imgsz += byte_to_write;
        if (img_sign >= 8)
            wait_target_img = false;
        return iBlockSize;
    }

    WriteLen = iBlockSize;
    offset = iOffset - aws_ota_target_hdr.FileImgHdr.Offset;
    if ((offset + iBlockSize) >= aws_ota_target_hdr.FileImgHdr.ImgLen) {
        WriteLen -= (offset + iBlockSize - aws_ota_target_hdr.FileImgHdr.ImgLen);
        OTA_PRINT("[OTA] LAST image data arrived %d\n", WriteLen);
    }
    OTA_PRINT("[OTA] Write %d bytes @ 0x%x\n", WriteLen, address + offset);
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    if(flash_stream_write(&flash, address + offset, WriteLen, pacData) < 0){
        OTA_PRINT("[%s] Write sector failed\n", __FUNCTION__);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        return -1;
    }
#if OTA_DEBUG && OTA_MEMDUMP
    vMemDump(pacData, iBlockSize, "PAYLOAD");
#endif
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    aws_ota_imgsz += WriteLen;
    return iBlockSize;
}

OTA_Err_t prvPAL_ActivateNewImage_rtl8710b(void)
{
    flash_t flash;
    /* Reset the MCU so we can test the new image. Short delay for debug log output. */
    OTA_PRINT("[OTA] [%s] Download new firmware %d bytes completed @ 0x%x\n", __FUNCTION__, aws_ota_imgsz, aws_ota_target_hdr.FileImgHdr.FlashOffset);
    OTA_PRINT("[OTA] signature: %s, size = %d, OtaTargetHdr.FileImgHdr.ImgLen = %d\n", aws_ota_signature, aws_ota_imgsz, aws_ota_target_hdr.FileImgHdr.ImgLen);
    /*------------- verify checksum and update signature-----------------*/
    if(verify_ota_checksum(aws_ota_target_hdr.FileImgHdr.FlashOffset, aws_ota_imgsz - 8, aws_ota_signature, &aws_ota_target_hdr)){
        if(!change_ota_signature(aws_ota_target_hdr.FileImgHdr.FlashOffset, aws_ota_signature, http_ota_target_index)) {
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
        flash_erase_sector(&flash, aws_ota_target_hdr.FileImgHdr.FlashOffset - SPI_FLASH_BASE);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        OTA_PRINT("[OTA] [%s] The checksume is wrong!\n\r", __FUNCTION__);
        return kOTA_Err_ActivateFailed;
    }
    OTA_PRINT("[OTA] Resetting MCU to activate new image.\r\n");
    vTaskDelay( 500 );
    prvSysReset_rtl8710b();
    return kOTA_Err_None;
}

OTA_Err_t prvPAL_ResetDevice_rtl8710b ( void )
{
    prvSysReset_rtl8710b();
    return kOTA_Err_None;
}

OTA_Err_t prvPAL_SetPlatformImageState_rtl8710b (OTA_ImageState_t eState)
{
    DEFINE_OTA_METHOD_NAME( "prvSetImageState_rtl8710b" );
    OTA_Err_t eResult = kOTA_Err_Uninitialized;
    uint32_t ota_imagestate =  AWS_OTA_IMAGE_STATE_FLAG_IMG_INVALID;
    flash_t flash;
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &ota_imagestate);
    device_mutex_unlock(RT_DEV_LOCK_FLASH);
    //OTA_PRINT("[OTA] %s not implemented yet.\r\n", __func__);
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

#if testrunnerFULL_OTA_PAL_ENABLED
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
			device_mutex_lock(RT_DEV_LOCK_FLASH);
			OTA_Change(ota_target_reset_index);
			device_mutex_unlock(RT_DEV_LOCK_FLASH);
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

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_rtl8710b( void )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState_rtl8710b" );
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

u8 * prvReadAndAssumeCertificate_rtl8710b(const u8 * const pucCertName, s32 * const lSignerCertSize)
{
    OTA_PRINT( ("prvReadAndAssumeCertificate: not implemented yet\r\n") );
    return NULL;
}

