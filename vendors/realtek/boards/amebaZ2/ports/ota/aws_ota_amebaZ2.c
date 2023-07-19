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
#include "ota.h"
#include "ota_pal.h"
#include "ota_interface_private.h"
#include "ota_config.h"
#include "iot_crypto.h"
#include "core_pkcs11.h"
#include "platform_opts.h"
#include "flash_api.h"
#include <device_lock.h>
#include "ota_8710c.h"
#include "platform_stdlib.h"
#include "platform_opts.h"

//================================================================================

#define AWS_OTA_IMAGE_SIGNATURE_LEN                     32
#if 0 // move to platform_opts.h
#define AWS_OTA_IMAGE_STATE_FLASH_OFFSET        0x00003000 // Flash reserved section 0x0000_3000 - 0x0000_4000-1
#endif

extern uint32_t sys_update_ota_get_curr_fw_idx(void);
extern uint32_t sys_update_ota_prepare_addr(void);


static flash_t flash_ota;

unsigned char sig_backup[AWS_OTA_IMAGE_SIGNATURE_LEN];

typedef struct {
    int32_t lFileHandle;
} amebaz2_ota_context_t;

static amebaz2_ota_context_t ota_ctx;

#define OTA_MEMDUMP 0
#if OTA_MEMDUMP
void vMemDump(const u8 *start, u32 size, char * strHeader)
{
    int row, column, index, index2, max;
    u8 *buf, *line;

    if (!start || (size == 0)) {
        return;
    }

    line = (u8*)start;

    /*
    16 bytes per line
    */
    if (strHeader){
       printf ("%s", strHeader);
    }

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

    curr_fw_idx = sys_update_ota_get_curr_fw_idx();
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

static void prvSysReset_amebaZ2(void)
{
    extern void ota_platform_reset(void);
    ota_platform_reset();
}

OtaPalStatus_t prvPAL_Abort_amebaZ2(OtaFileContext_t *C)
{
    if (C != NULL && C->pFile != NULL) {
        LogInfo(("[%s] Abort OTA update", __FUNCTION__));
        C->pFile = NULL;
        ota_ctx.lFileHandle = 0x0;
    }
    return OTA_PAL_COMBINE_ERR(OtaPalSuccess, 0);
}

bool prvPAL_CreateFileForRx_amebaZ2(OtaFileContext_t *C)
{
    OtaPalMainStatus_t mainErr = OtaPalSuccess;
    OtaPalSubStatus_t subErr = 0;

    uint32_t curr_fw_idx = sys_update_ota_get_curr_fw_idx();

    LogInfo( ( "Current firmware index is %d", curr_fw_idx ) );

    C->pFile = (uint8_t*)&ota_ctx;
    ota_ctx.lFileHandle = sys_update_ota_prepare_addr() + SPI_FLASH_BASE;
    if(curr_fw_idx==1)
        LogInfo( ( "fw2 address 0x%x will be upgraded", ota_ctx.lFileHandle ) );
    else
        LogInfo( ( "fw1 address 0x%x will be upgraded", ota_ctx.lFileHandle ) );

    if(ota_ctx.lFileHandle <= SPI_FLASH_BASE)
        mainErr = OtaPalRxFileCreateFailed;

    return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

/* Read the specified signer certificate from the filesystem into a local buffer. The
 * allocated memory becomes the property of the caller who is responsible for freeing it.
 */
uint8_t * prvPAL_ReadAndAssumeCertificate_amebaZ2(const uint8_t * const pucCertName, int32_t * const lSignerCertSize)
{
    uint8_t*    pucCertData;
    uint32_t    ulCertSize;
    uint8_t     *pucSignerCert = NULL;

    extern BaseType_t PKCS11_PAL_GetObjectValue( const char * pcFileName,
                               uint8_t ** ppucData,
                               uint32_t * pulDataSize );

    if ( PKCS11_PAL_GetObjectValue( (const char *) pucCertName, &pucCertData, &ulCertSize ) != pdTRUE )
    {   /* Use the back up "codesign_keys.h" file if the signing credentials haven't been saved in the device. */
        pucCertData = (uint8_t*) otapalconfigCODE_SIGNING_CERTIFICATE;
        ulCertSize = sizeof( otapalconfigCODE_SIGNING_CERTIFICATE );
        LogInfo( ( "Assume Cert - No such file: %s. Using header file", (const char*)pucCertName ) );
    }
    else
    {
        LogInfo( ( "Assume Cert - file: %s OK", (const char*)pucCertName ) );
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
    return pucSignerCert;
}

static OtaPalStatus_t prvSignatureVerificationUpdate_amebaZ2(OtaFileContext_t *C, void * pvContext)
{
    OtaPalMainStatus_t mainErr = OtaPalSuccess;
    OtaPalSubStatus_t subErr = 0;

    int firmware_len = (C->fileSize % OTA_FILE_BLOCK_SIZE) == 4 ? (C->fileSize - 4) :
                        C->fileSize;  // if fw length % 4096 = 4, it may include 4 bytes checksum at the end of fw
    int chklen = firmware_len;	  // skip 4byte ota length

    uint8_t *pTempbuf = pvPortMalloc(OTA_FILE_BLOCK_SIZE);

    uint32_t addr = ota_ctx.lFileHandle - SPI_FLASH_BASE;

    uint32_t cur_block = 0;
    while (chklen > 0) {
        int rdlen = chklen > OTA_FILE_BLOCK_SIZE ? OTA_FILE_BLOCK_SIZE : chklen;
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_stream_read(&flash_ota, addr + cur_block * OTA_FILE_BLOCK_SIZE, rdlen, pTempbuf);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        if (chklen == firmware_len) {   // for first block
            /* update sigature from backup buffer */
            CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)sig_backup, AWS_OTA_IMAGE_SIGNATURE_LEN);
            /* update content */
            CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf + AWS_OTA_IMAGE_SIGNATURE_LEN, rdlen - AWS_OTA_IMAGE_SIGNATURE_LEN);
        } else {
            /* update content */
            CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf, rdlen);
        }
        chklen -= rdlen;
        cur_block++;
    }

exit:
    if(pTempbuf)
        update_free(pTempbuf);

    return OTA_PAL_COMBINE_ERR( mainErr, subErr );
}

extern void *calloc_freertos(size_t nelements, size_t elementSize);
OtaPalStatus_t prvPAL_SetPlatformImageState_amebaZ2 (OtaImageState_t eState);
OtaPalStatus_t prvPAL_CheckFileSignature_amebaZ2(OtaFileContext_t * const C)
{
    OtaPalMainStatus_t mainErr = OtaPalSuccess;
    OtaPalSubStatus_t subErr = 0;

    int32_t lSignerCertSize;
    void *pvSigVerifyContext;
    uint8_t *pucSignerCert = NULL;

    mbedtls_platform_set_calloc_free(calloc_freertos, vPortFree);

    /* Verify an ECDSA-SHA256 signature. */
    if (CRYPTO_SignatureVerificationStart(&pvSigVerifyContext, cryptoASYMMETRIC_ALGORITHM_ECDSA, cryptoHASH_ALGORITHM_SHA256) == pdFALSE) {
        mainErr = OtaPalSignatureCheckFailed;
        goto exit;
    }

    LogInfo(("[%s] Started %s signature verification, file: %s", __FUNCTION__, OTA_JsonFileSignatureKey, (const char *)C->pCertFilepath));

    /* read certificate for verification */
    if ((pucSignerCert = prvPAL_ReadAndAssumeCertificate_amebaZ2((const uint8_t *const)C->pCertFilepath, &lSignerCertSize)) == NULL) {
        mainErr = OtaPalBadSignerCert;
        goto exit;
    }

    /* update for integrety check */
    if (OTA_PAL_MAIN_ERR(prvSignatureVerificationUpdate_amebaZ2(C, pvSigVerifyContext)) != OtaPalSuccess) {
        mainErr = OtaPalSignatureCheckFailed;
        goto exit;
    }

    /* verify the signature for integrety validation */
    if (CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char *)pucSignerCert, lSignerCertSize, C->pSignature->data, C->pSignature->size) == pdFALSE) {
        mainErr = OtaPalSignatureCheckFailed;
        prvPAL_SetPlatformImageState_amebaZ2(OtaImageStateRejected);
        goto exit;
    }

exit:
    /* Free the signer certificate that we now own after prvPAL_ReadAndAssumeCertificate(). */
    if (pucSignerCert != NULL) {
        vPortFree(pucSignerCert);
    }
    return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

/* Close the specified file. This will also authenticate the file if it is marked as secure. */
OtaPalStatus_t prvPAL_CloseFile_amebaZ2(OtaFileContext_t *C)
{
    OtaPalStatus_t mainErr = OtaPalSuccess;
    OtaPalSubStatus_t subErr = 0;

    LogInfo(("[OTA] Authenticating and closing file.\r\n"));

    if (C == NULL) {
        mainErr = OtaPalNullFileContext;
        goto exit;
    }

    /* close the fw file */
    if (C->pFile) {
        C->pFile = NULL;
    }

    if (C->pSignature != NULL) {
#if OTA_MEMDUMP
        vMemDump(C->pSignature->data, C->pSignature->size, "Signature");
#endif
        /* TODO: Verify the file signature, close the file and return the signature verification result. */
        mainErr = OTA_PAL_MAIN_ERR(prvPAL_CheckFileSignature_amebaZ2(C));

    } else {
        mainErr = OtaPalSignatureCheckFailed;
    }

    if (mainErr == OtaPalSuccess) {
        LogInfo(("[%s] %s signature verification passed.", __FUNCTION__, OTA_JsonFileSignatureKey));
    } else {
        LogError(("[%s] Failed to pass %s signature verification: %d.", __FUNCTION__, OTA_JsonFileSignatureKey, mainErr));

        /* If we fail to verify the file signature that means the image is not valid. We need to set the image state to aborted. */
        prvPAL_SetPlatformImageState_amebaZ2(OtaImageStateAborted);
    }

exit:

    return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

int16_t prvPAL_WriteBlock_amebaZ2(OtaFileContext_t *C, uint32_t ulOffset, uint8_t* pData, uint32_t ulBlockSize)
{
    static uint32_t buf_size = OTA_FILE_BLOCK_SIZE;
    static uint32_t ota_len = 0;
    static uint32_t total_blocks = 0;
    static uint32_t cur_block = 0;
    static uint32_t idx = 0;
    static uint32_t read_bytes = 0;
    static uint8_t *buf = NULL;
    static uint32_t ota_address = 0;
    static uint8_t first_time = 0;

    if (C == NULL) {
        return -1;
    }

    LogInfo(("[%s] C->fileSize %d, iOffset: 0x%x: iBlockSize: 0x%x", __FUNCTION__, C->fileSize, ulOffset, ulBlockSize));

    ota_address = ota_ctx.lFileHandle - SPI_FLASH_BASE;
    buf_size = OTA_FILE_BLOCK_SIZE;
    ota_len = C->fileSize;
    total_blocks = (ota_len + buf_size - 1) / buf_size;
    idx = ulOffset;
    read_bytes = ulBlockSize;
    cur_block = idx / buf_size;
    buf = pData;

    LogInfo(("[%s] ota_len:%d, cur_block:%d", __FUNCTION__, ota_len, cur_block));
#if OTA_MEMDUMP
    vMemDump(pData, ulBlockSize, "PAYLOAD0");
#endif

    if(first_time==0) {
    	LogInfo(("[%s] update_ota_erase_upg_region", __FUNCTION__, idx, ota_len));
        update_ota_erase_upg_region(C->fileSize, 0, sys_update_ota_prepare_addr());
        first_time = 1;
    }

    if (idx >= ota_len) {
        LogInfo(("[%s] image download is already done, dropped, idx=0x%X, ota_len=0x%X", __FUNCTION__, idx, ota_len));
        goto exit;
    }

    // for first block
    if (0 == cur_block) {
        LogInfo(("[%s] FIRST image data arrived %d, back up the first 32-bytes fw signature", __FUNCTION__, read_bytes));

        memcpy(sig_backup, pData, AWS_OTA_IMAGE_SIGNATURE_LEN);
        memset(buf, 0xFF, AWS_OTA_IMAGE_SIGNATURE_LEN); // not flash write 8-bytes fw label
        LogInfo(("[%s] sig_backup get", __FUNCTION__));
    }

    // check final block
    if (cur_block == (total_blocks - 1)) {
        LogInfo(("[%s] LAST image data arrived", __FUNCTION__));
    }

    /* write block by flash api */
    device_mutex_lock(RT_DEV_LOCK_FLASH);
    LogInfo(("[OTA] Write %d bytes @ 0x%x (0x%x)\n", read_bytes, ota_address + ulOffset, (ota_address + ulOffset)));
    if(flash_stream_write(&flash_ota, ota_address + ulOffset, read_bytes, buf) < 0){
        LogInfo(("[%s] Write sector failed\n", __FUNCTION__));
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
        return -1;
    }
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

exit:

    LogInfo(("[%s] Write bytes: read_bytes %d, ulBlockSize %d", __FUNCTION__, read_bytes, ulBlockSize));
    return ulBlockSize;
}

OtaPalStatus_t prvPAL_ActivateNewImage_amebaZ2(void)
{
    int ret = -1;
    uint32_t NewFWAddr = 0;

    NewFWAddr = sys_update_ota_prepare_addr();
    if(NewFWAddr == -1){
        return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
    }

    ret = update_ota_signature(sig_backup, NewFWAddr);
    if(ret == -1){
        LogInfo(("[%s] Update signature fail\r\n", __FUNCTION__));
        return OTA_PAL_COMBINE_ERR( OtaPalActivateFailed, 0 );
    }
    else{
        prvPAL_SetPlatformImageState_amebaZ2(OtaImageStateTesting);
        LogInfo(("[OTA] [%s] Update OTA success!\r\n", __FUNCTION__));
    }

    LogInfo(("[OTA] Resetting MCU to activate new image.\r\n"));
    vTaskDelay( 100 );
    prvSysReset_amebaZ2();
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

OtaPalStatus_t prvPAL_ResetDevice_amebaZ2 (void)
{
    prvSysReset_amebaZ2();
    return OTA_PAL_COMBINE_ERR( OtaPalSuccess, 0 );
}

OtaPalStatus_t prvPAL_SetPlatformImageState_amebaZ2 (OtaImageState_t eState)
{
    LogInfo(("%s", __FUNCTION__));

    OtaPalMainStatus_t mainErr = OtaPalSuccess;
    OtaPalSubStatus_t subErr = 0;

    if ((eState != OtaImageStateUnknown) && (eState <= OtaLastImageState)) {
        /* write state to file */
        device_mutex_lock(RT_DEV_LOCK_FLASH);
        flash_erase_sector(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET);
        flash_write_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, eState);
        device_mutex_unlock(RT_DEV_LOCK_FLASH);
    } else { /* Image state invalid. */
        LogError(("[%s] Invalid image state provided.", __FUNCTION__));
        mainErr = OtaPalBadImageState;
    }

    return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

OtaPalImageState_t prvPAL_GetPlatformImageState_amebaZ2( void )
{
    OtaPalImageState_t eImageState = OtaPalImageStateUnknown;
    uint32_t eSavedAgentState  =  OtaImageStateUnknown;

    device_mutex_lock(RT_DEV_LOCK_FLASH);
    flash_read_word(&flash_ota, AWS_OTA_IMAGE_STATE_FLASH_OFFSET, &eSavedAgentState );
    device_mutex_unlock(RT_DEV_LOCK_FLASH);

    switch ( eSavedAgentState  )
    {
        case OtaImageStateTesting:
            /* Pending Commit means we're in the Self Test phase. */
            eImageState = OtaPalImageStatePendingCommit;
            break;
        case OtaImageStateAccepted:
            eImageState = OtaPalImageStateValid;
            break;
        case OtaImageStateRejected:
        case OtaImageStateAborted:
        default:
            eImageState = OtaPalImageStateInvalid;
            break;
    }
    LogInfo( ( "Image current state (0x%02x).", eImageState ) );

    return eImageState;
}

u8 * prvReadAndAssumeCertificate_amebaZ2(const u8 * const pucCertName, s32 * const lSignerCertSize)
{
    LogInfo( ("prvReadAndAssumeCertificate: not implemented yet\r\n") );
    return NULL;
}

