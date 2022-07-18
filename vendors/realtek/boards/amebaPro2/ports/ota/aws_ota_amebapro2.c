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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* OTA PAL implementation for Realtek Ameba platform. */
#include "ota.h"
#include "ota_pal.h"
#include "ota_interface_private.h"
#include "ota_config.h"
#include "iot_crypto.h"
#include "core_pkcs11.h"

//================================================================================
/* AmebaPro2 */
#include <sys.h>
#include "platform_stdlib.h"
#include "ota_8735b.h"
#include "hal_flash_boot.h"
#include "fwfs.h"
#include <device_lock.h>
#include "sys_api.h"
#include "flash_api.h"

#define RTK_OTA_IMAGE_LABEL_LEN     8

static uint32_t gNewImgLen = 0;
static _file_checksum file_checksum;
static unsigned char label_backup[RTK_OTA_IMAGE_LABEL_LEN];
static unsigned char label_readback[RTK_OTA_IMAGE_LABEL_LEN];

#define OTA_STATE_FILE      "sd:/ota_state.dat"

#define FLASH_API_MODE      0x01
#define FWFS_API_MODE       0x02
#define WRITE_BLOCK_MODE    FLASH_API_MODE

typedef struct nor_flash_info_s {
	uint32_t target_fw_addr;
	uint32_t target_fw_len;
	flash_t flash;
} nor_flash_info_t;

static nor_flash_info_t aws_flash_info;

//================================================================================

#define OTA_MEMDUMP     0

#if OTA_MEMDUMP
void vMemDump(const u8 *start, u32 size, char *strHeader)
{
	int row, column, index, index2, max;
	u8 *buf, *line;

	if (!start || (size == 0)) {
		return;
	}

	line = (u8 *)start;

	/*
	16 bytes per line
	*/
	if (strHeader) {
		printf("%s", strHeader);
	}

	column = size % 16;
	row = (size / 16) + 1;
	for (index = 0; index < row; index++, line += 16) {
		buf = (u8 *)line;

		max = (index == row - 1) ? column : 16;
		if (max == 0) {
			break;    /* If we need not dump this line, break it. */
		}

		printf("\n[%08x] ", line);

		//Hex
		for (index2 = 0; index2 < max; index2++) {
			if (index2 == 8) {
				printf("  ");
			}
			printf("%02x ", (u8) buf[index2]);
		}

		if (max != 16) {
			if (max < 8) {
				printf("  ");
			}
			for (index2 = 16 - max; index2 > 0; index2--) {
				printf("   ");
			}
		}

	}

	printf("\n");
	return;
}
#endif

//================================================================================

OtaPalStatus_t prvPAL_ResetDevice_amebaPro2(void)
{
	ota_platform_reset();
	return OTA_PAL_COMBINE_ERR(OtaPalSuccess, 0);
}

OtaPalStatus_t prvPAL_Abort_amebaPro2(OtaFileContext_t *C)
{
	if (C != NULL && C->pFile != NULL) {
		LogInfo(("[%s] Abort OTA update", __FUNCTION__));
#if WRITE_BLOCK_MODE==FLASH_API_MODE
		C->pFile = NULL;
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
		pfw_close(C->pFile);
#endif
	}
	return OTA_PAL_COMBINE_ERR(OtaPalSuccess, 0);
}

OtaPalStatus_t prvPAL_CreateFileForRx_amebaPro2(OtaFileContext_t *C)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	if (C == NULL) {
		LogError(("[%s] Invalid context provided.", __FUNCTION__));
		mainErr = OtaPalRxFileCreateFailed;
		goto exit;
	}

	/* check current flash index */
	uint32_t target_fw_idx;
	uint32_t curr_fw_idx = hal_sys_get_ld_fw_idx();
	LogInfo(("[%s] Current firmware index is %d", __FUNCTION__, curr_fw_idx));
	if (1 == curr_fw_idx) {
		target_fw_idx = 2;
	} else if (2 == curr_fw_idx) {
		target_fw_idx = 1;
	}

	/* open flash for write block */
#if WRITE_BLOCK_MODE==FLASH_API_MODE
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	if (1 == target_fw_idx) {
		// fw1 record in partition table
		flash_read_word(&aws_flash_info.flash, 0x2060, &aws_flash_info.target_fw_addr);
		flash_read_word(&aws_flash_info.flash, 0x2064, &aws_flash_info.target_fw_len);
	} else if (2 == target_fw_idx) {
		// fw2 record in partition table
		flash_read_word(&aws_flash_info.flash, 0x2080, &aws_flash_info.target_fw_addr);
		flash_read_word(&aws_flash_info.flash, 0x2084, &aws_flash_info.target_fw_len);
	}
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
	LogInfo(("[%s] target_fw_addr=0x%x, target_fw_len=0x%x [%s]", __FUNCTION__, aws_flash_info.target_fw_addr, aws_flash_info.target_fw_len, __FUNCTION__));
	C->pFile = (void *)&aws_flash_info;
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
	char *part_name;
	if (curr_fw_idx == 1) {
		LogInfo(("[%s] fw2 address will be upgraded", __FUNCTION__));
		part_name = "FW2";
	} else {
		LogInfo(("[%s] fw1 address will be upgraded", __FUNCTION__));
		part_name = "FW1";
	}
	C->pFile = pfw_open(part_name, M_RAW | M_RDWR);
	if (!C->pFile) {
		LogError(("[%s] Failed to open fw file system for OTA", __FUNCTION__));
		mainErr = OtaPalRxFileCreateFailed;
		goto exit;
	}
	pfw_seek(C->pFile, 0, SEEK_SET);
#endif

	if (C->pFile) {
		file_checksum.u = 0; //reset checksum
	}

exit:

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

/* Read the specified signer certificate from the filesystem into a local buffer. The
 * allocated memory becomes the property of the caller who is responsible for freeing it.
 */
extern BaseType_t PKCS11_PAL_GetObjectValue(const char *pcFileName, uint8_t **ppucData,	uint32_t *pulDataSize);

uint8_t *prvPAL_ReadAndAssumeCertificate_amebaPro2(const uint8_t *const pucCertName, uint32_t *const lSignerCertSize)
{
	uint8_t *pucCertData;
	uint32_t ulCertSize;
	uint8_t  *pucSignerCert = NULL;

	if (PKCS11_PAL_GetObjectValue((const char *) pucCertName, &pucCertData, &ulCertSize) != pdTRUE) {
		/* Use the back up "codesign_keys.h" file if the signing credentials haven't been saved in the device. */
		pucCertData = (uint8_t *) otapalconfigCODE_SIGNING_CERTIFICATE;
		ulCertSize = sizeof(otapalconfigCODE_SIGNING_CERTIFICATE);
		LogInfo(("[%s] Assume Cert - No such file: %s. Using header file", __FUNCTION__, (const char *)pucCertName));
	} else {
		LogInfo(("[%s] Assume Cert - file: %s OK", __FUNCTION__, (const char *)pucCertName));
	}

	/* Allocate memory for the signer certificate plus a terminating zero so we can load it and return to the caller. */
	pucSignerCert = pvPortMalloc(ulCertSize + 1);
	if (pucSignerCert != NULL) {
		memcpy(pucSignerCert, pucCertData, ulCertSize);
		/* The crypto code requires the terminating zero to be part of the length so add 1 to the size. */
		pucSignerCert[ ulCertSize ] = '\0';
		*lSignerCertSize = ulCertSize + 1;
	}
	return pucSignerCert;
}

static OtaPalStatus_t prvSignatureVerificationUpdate_amebaPro2(OtaFileContext_t *C, void *pvContext)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	uint32_t curr_fw_idx = hal_sys_get_ld_fw_idx();
	char *part_name;
	if (curr_fw_idx == 1) {
		LogInfo(("[%s] fw2 address will be upgraded", __FUNCTION__));
		part_name = "FW2";
	} else {
		LogInfo(("[%s] fw1 address will be upgraded", __FUNCTION__));
		part_name = "FW1";
	}

	uint32_t chksum = 0;
	int chklen = gNewImgLen - 4;    // skip 4byte ota length
	uint8_t *pTempbuf = pvPortMalloc(OTA_FILE_BLOCK_SIZE);

#if WRITE_BLOCK_MODE==FLASH_API_MODE
	uint32_t cur_block = 0;
	while (chklen > 0) {
		int rdlen = chklen > OTA_FILE_BLOCK_SIZE ? OTA_FILE_BLOCK_SIZE : chklen;
		device_mutex_lock(RT_DEV_LOCK_FLASH);
		flash_stream_read(&aws_flash_info.flash, aws_flash_info.target_fw_addr + cur_block * NOR_BLOCK_SIZE, rdlen, pTempbuf);
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		if (chklen == (gNewImgLen - 4)) {   // for first block
			/* update label from backup buffer */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)label_backup, RTK_OTA_IMAGE_LABEL_LEN);
			/* update content */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf + RTK_OTA_IMAGE_LABEL_LEN, rdlen - RTK_OTA_IMAGE_LABEL_LEN);
		} else {
			/* update content */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf, rdlen);
		}
		chklen -= rdlen;
		cur_block++;
	}
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
	void *chkfp = pfw_open(part_name, M_RAW | M_RDWR);
	if (!chkfp) {
		mainErr = OtaPalSignatureCheckFailed;
		goto exit;
	}
	pfw_seek(chkfp, 0, SEEK_SET);
	while (chklen > 0) {
		int rdlen = chklen > OTA_FILE_BLOCK_SIZE ? OTA_FILE_BLOCK_SIZE : chklen;
		pfw_read(chkfp, pTempbuf, rdlen);
		if (chklen == (gNewImgLen - 4)) {   // for first block
			/* update label from backup buffer */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)label_backup, RTK_OTA_IMAGE_LABEL_LEN);
			/* update content */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf + RTK_OTA_IMAGE_LABEL_LEN, rdlen - RTK_OTA_IMAGE_LABEL_LEN);
		} else {
			/* update content */
			CRYPTO_SignatureVerificationUpdate(pvContext, (uint8_t *)pTempbuf, rdlen);
		}
		chklen -= rdlen;
	}
	pfw_close(chkfp);
#endif

	LogInfo(("[%s] Signature Verification Update done.", __FUNCTION__));

exit:
	if (pTempbuf) {
		vPortFree(pTempbuf);
	}

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

OtaPalStatus_t prvPAL_SetPlatformImageState_amebaPro2(OtaImageState_t eState);

OtaPalStatus_t prvPAL_CheckFileSignature_amebaPro2(OtaFileContext_t *const C)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	uint32_t lSignerCertSize;
	void *pvSigVerifyContext;
	uint8_t *pucSignerCert = NULL;

	/* Verify an ECDSA-SHA256 signature. */
	if (CRYPTO_SignatureVerificationStart(&pvSigVerifyContext, cryptoASYMMETRIC_ALGORITHM_ECDSA, cryptoHASH_ALGORITHM_SHA256) == pdFALSE) {
		mainErr = OtaPalSignatureCheckFailed;
		goto exit;
	}

	LogInfo(("[%s] Started %s signature verification, file: %s", __FUNCTION__, OTA_JsonFileSignatureKey, (const char *)C->pCertFilepath));

	/* read certificate for verification */
	if ((pucSignerCert = prvPAL_ReadAndAssumeCertificate_amebaPro2((const uint8_t *const)C->pCertFilepath, &lSignerCertSize)) == NULL) {
		mainErr = OtaPalBadSignerCert;
		goto exit;
	}

	/* update for integrety check */
	if (OTA_PAL_MAIN_ERR(prvSignatureVerificationUpdate_amebaPro2(C, pvSigVerifyContext)) != OtaPalSuccess) {
		mainErr = OtaPalSignatureCheckFailed;
		goto exit;
	}

	/* verify the signature for integrety validation */
	if (CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char *)pucSignerCert, lSignerCertSize, C->pSignature->data, C->pSignature->size) == pdFALSE) {
		mainErr = OtaPalSignatureCheckFailed;
		prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateRejected);
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
OtaPalStatus_t prvPAL_CloseFile_amebaPro2(OtaFileContext_t *C)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	LogInfo(("[%s] Authenticating and closing file.", __FUNCTION__));

	if (C == NULL) {
		mainErr = OtaPalNullFileContext;
		goto exit;
	}

	/* close the fw file */
	if (C->pFile) {
#if WRITE_BLOCK_MODE==FLASH_API_MODE
		C->pFile = NULL;
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
		pfw_close(C->pFile);
#endif
	}

	if (C->pSignature != NULL) {
#if OTA_MEMDUMP
		vMemDump(C->pSignature->data, C->pSignature->size, "Signature");
#endif
		/* TODO: Verify the file signature, close the file and return the signature verification result. */
		mainErr = OTA_PAL_MAIN_ERR(prvPAL_CheckFileSignature_amebaPro2(C));

	} else {
		mainErr = OtaPalSignatureCheckFailed;
	}

	if (mainErr == OtaPalSuccess) {
		LogInfo(("[%s] %s signature verification passed.", __FUNCTION__, OTA_JsonFileSignatureKey));
	} else {
		LogError(("[%s] Failed to pass %s signature verification: %d.", __FUNCTION__, OTA_JsonFileSignatureKey, mainErr));

		/* If we fail to verify the file signature that means the image is not valid. We need to set the image state to aborted. */
		prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateAborted);
	}

exit:

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

int16_t prvPAL_WriteBlock_amebaPro2(OtaFileContext_t *C, uint32_t ulOffset, uint8_t *pData, uint32_t ulBlockSize)
{
	static uint32_t buf_size = OTA_FILE_BLOCK_SIZE;
	static uint32_t ota_len = 0;
	static uint32_t total_blocks = 0;
	static uint32_t cur_block = 0;
	static uint32_t idx = 0;
	static uint32_t read_bytes = 0;
	static uint8_t *buf = NULL;

	if (C == NULL) {
		return -1;
	}

	LogInfo(("[%s] C->fileSize %d, iOffset: 0x%x: iBlockSize: 0x%x", __FUNCTION__, C->fileSize, ulOffset, ulBlockSize));

	// mapping parameters to Pro2 SDK OTA example
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

	if (idx >= ota_len) {
		LogInfo(("[%s] image download is already done, dropped, idx=0x%X, ota_len=0x%X", __FUNCTION__, idx, ota_len));
		goto exit;
	}

	// checksum attached at file end
	if ((idx + read_bytes) > (ota_len - 4)) {
		file_checksum.c[0] = buf[read_bytes - 4];
		file_checksum.c[1] = buf[read_bytes - 3];
		file_checksum.c[2] = buf[read_bytes - 2];
		file_checksum.c[3] = buf[read_bytes - 1];
	}

	// for first block
	if (0 == cur_block) {
		LogInfo(("[%s] FIRST image data arrived %d, back up the first 8-bytes fw label", __FUNCTION__, read_bytes));
		memcpy(label_backup, buf, RTK_OTA_IMAGE_LABEL_LEN); // save 8-bytes fw label
		memset(buf, 0xFF, RTK_OTA_IMAGE_LABEL_LEN); // not flash write 8-bytes fw label
		LogInfo(("[%s] label_backup get [%llu]", __FUNCTION__, label_backup));
		gNewImgLen = ota_len;
	}

	// check final block
	if (cur_block == (total_blocks - 1)) {
		read_bytes -= 4; // remove final 4 bytes checksum
		LogInfo(("[%s] LAST image data arrived %d", __FUNCTION__, read_bytes));
	}

#if WRITE_BLOCK_MODE==FLASH_API_MODE
	/* write block by flash api */
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	flash_erase_sector(&aws_flash_info.flash, aws_flash_info.target_fw_addr + cur_block * NOR_BLOCK_SIZE);
	if (flash_burst_write(&aws_flash_info.flash, aws_flash_info.target_fw_addr + cur_block * NOR_BLOCK_SIZE, read_bytes, buf) < 0) {
		LogInfo(("[%s] flash write failed", __FUNCTION__));
		device_mutex_unlock(RT_DEV_LOCK_FLASH);
		return -1;
	}
	device_mutex_unlock(RT_DEV_LOCK_FLASH);
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
	/* write block by fwfs */
	int wr_status = pfw_write(C->pFile, buf, read_bytes);
	if (wr_status < 0) {
		LogInfo(("[%s] ota flash failed", __FUNCTION__));
		return -1;
	}
#endif

exit:

	LogInfo(("[%s] Write bytes: read_bytes %d, ulBlockSize %d", __FUNCTION__, read_bytes, ulBlockSize));
	return ulBlockSize;
}

OtaPalStatus_t prvPAL_ActivateNewImage_amebaPro2(void)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	/* get target fw name */
	uint32_t curr_fw_idx = hal_sys_get_ld_fw_idx();
	char *part_name;
	if (curr_fw_idx == 1) {
		LogInfo(("[%s] fw2 need to be activated!", __FUNCTION__));
		part_name = "FW2";
	} else {
		LogInfo(("[%s] fw1 need to be activated!", __FUNCTION__));
		part_name = "FW1";
	}

	/* write back 8-bytes fw label to mark target flash as valid */
#if WRITE_BLOCK_MODE==FLASH_API_MODE
	LogInfo(("[%s] Append FW label", __FUNCTION__));
	device_mutex_lock(RT_DEV_LOCK_FLASH);
	if (flash_burst_write(&aws_flash_info.flash, aws_flash_info.target_fw_addr, 8, label_backup) < 0) {
		LogError(("[%s] Failed to write flash for OTA label", __FUNCTION__));
		mainErr = OtaPalActivateFailed;
		goto exit;
	}
	flash_stream_read(&aws_flash_info.flash, aws_flash_info.target_fw_addr, 8, label_readback);
	device_mutex_unlock(RT_DEV_LOCK_FLASH);

	LogInfo(("[%s] FW label:", __FUNCTION__));
	for (int i = 0; i < 8; i ++) {
		printf(" %02X", label_readback[i]);
	}
	printf("\n\r");
#elif WRITE_BLOCK_MODE==FWFS_API_MODE
	void *fp = pfw_open(part_name, M_RAW | M_RDWR);
	if (!fp) {
		LogError(("[%s] Failed to open fw file system for OTA label", __FUNCTION__));
		mainErr = OtaPalActivateFailed;
		goto exit;
	}
	pfw_seek(fp, 0, SEEK_SET);
	int wr_status = pfw_write(fp, label_backup, RTK_OTA_IMAGE_LABEL_LEN);
	if (wr_status < 0) {
		LogError(("[%s] ota write label_backup failed", __FUNCTION__));
		mainErr = OtaPalActivateFailed;
		goto exit;
	}
	pfw_close(fp);
#endif

	/* set state to pending commit for self testing */
	prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateTesting);

	/* reset device */
	LogInfo(("[%s] Resetting MCU to activate new image.", __FUNCTION__));
	vTaskDelay(100);
	prvPAL_ResetDevice_amebaPro2();
	/* should not run here */
	mainErr = OtaPalSuccess;

exit:

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

OtaPalStatus_t prvPAL_SetPlatformImageState_amebaPro2(OtaImageState_t eState)
{
	LogInfo(("%s", __FUNCTION__));

	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	if ((eState != OtaImageStateUnknown) && (eState <= OtaLastImageState)) {
		/* write state to file */
		FILE *fp = fopen(OTA_STATE_FILE, "wb+");
		if (fp == NULL) {
			LogError(("[%s] OTA_STATE_FILE open fail", __FUNCTION__));
			mainErr = OtaPalBadImageState;
		} else {
			fwrite(&eState, sizeof(OtaImageState_t), 1, fp);
			fclose(fp);
		}
	} else { /* Image state invalid. */
		LogError(("[%s] Invalid image state provided.", __FUNCTION__));
		mainErr = OtaPalBadImageState;
	}

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

OtaPalImageState_t prvPAL_GetPlatformImageState_amebaPro2(void)
{
	OtaPalImageState_t eImageState = OtaPalImageStateUnknown;

	/* read the state from file */
	OtaImageState_t eSavedAgentState = OtaImageStateUnknown;
	FILE *fp = fopen(OTA_STATE_FILE, "r+");
	if (fp == NULL) {
		LogError(("[%s] open OTA_STATE_FILE fail", __FUNCTION__));
		eImageState = OtaPalImageStateInvalid;
		goto exit;
	}
	fread(&eSavedAgentState, sizeof(OtaImageState_t), 1, fp);
	fclose(fp);

	switch (eSavedAgentState) {
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

exit:
	LogInfo(("[%s] Image current state (0x%02x).", __FUNCTION__, eImageState));

	return eImageState;
}

uint8_t *prvReadAndAssumeCertificate_amebaPro2(const uint8_t *const pucCertName, uint32_t *const lSignerCertSize)
{
	LogInfo(("[%s] prvReadAndAssumeCertificate: not implemented yet.", __FUNCTION__));
	return NULL;
}

