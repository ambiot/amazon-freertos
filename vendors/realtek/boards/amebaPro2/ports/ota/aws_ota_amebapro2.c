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

//================================================================================

/* AmebaPro2 */
#include "platform_stdlib.h"
#include "ota_8735b.h"
#include "hal_flash_boot.h"
#include "ftl_common_api.h"
#include "fwfs.h"

#define RTK_OTA_IMAGE_LABEL_LEN                     8
#define RTK_OTA_IMAGE_STATE_FLASH_OFFSET            0xF00000 + 0x5000 // Fix here!

static uint32_t gNewImgLen = 0;
static _file_checksum file_checksum;
unsigned char label_backup[RTK_OTA_IMAGE_LABEL_LEN];

/* test */
static uint32_t gCheckSum = 0;

//================================================================================

#define OTA_DEBUG       0
#define OTA_MEMDUMP     0
#define OTA_PRINT       printf

#if OTA_DEBUG && OTA_MEMDUMP
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

static void prvSysReset_amebaPro2(void)
{
	ota_platform_reset();
}

OtaPalStatus_t prvPAL_Abort_amebaPro2(OtaFileContext_t *C)
{
	if (C != NULL && C->pFile != NULL) {
		LogInfo(("Abort OTA update"));
		pfw_close(C->pFile);
	}
	return OTA_PAL_COMBINE_ERR(OtaPalSuccess, 0);
}

OtaPalStatus_t prvPAL_CreateFileForRx_amebaPro2(OtaFileContext_t *C)
{
	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	if (C == NULL) {
		LogError(("Invalid context provided."));
		mainErr = OtaPalRxFileCreateFailed;
		goto exit;
	}

	uint32_t curr_fw_idx = hal_sys_get_ld_fw_idx();
	LogInfo(("Current firmware index is %d", curr_fw_idx));

	char *part_name;
	if (curr_fw_idx == 1) {
		LogInfo(("fw2 address will be upgraded"));
		part_name = "FW2";
	} else {
		LogInfo(("fw1 address will be upgraded"));
		part_name = "FW1";
	}

	C->pFile = pfw_open(part_name, M_RAW | M_RDWR);
	if (!C->pFile) {
		LogError(("Failed to open fw file system for OTA"));
		mainErr = OtaPalRxFileCreateFailed;
		goto exit;
	}
	pfw_seek(C->pFile, 0, SEEK_SET);

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

uint8_t *prvPAL_ReadAndAssumeCertificate_amebaPro2(const uint8_t *const pucCertName, int32_t *const lSignerCertSize)
{
	uint8_t *pucCertData;
	uint32_t ulCertSize;
	uint8_t  *pucSignerCert = NULL;

	if (PKCS11_PAL_GetObjectValue((const char *) pucCertName, &pucCertData, &ulCertSize) != pdTRUE) {
		/* Use the back up "codesign_keys.h" file if the signing credentials haven't been saved in the device. */
		pucCertData = (uint8_t *) otapalconfigCODE_SIGNING_CERTIFICATE;
		ulCertSize = sizeof(otapalconfigCODE_SIGNING_CERTIFICATE);
		LogInfo(("Assume Cert - No such file: %s. Using header file", (const char *)pucCertName));
	} else {
		LogInfo(("Assume Cert - file: %s OK", (const char *)pucCertName));
	}

	/* Allocate memory for the signer certificate plus a terminating zero so we can load it and return to the caller. */
	pucSignerCert = pvPortMalloc(ulCertSize +	1);
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
		LogInfo(("fw2 address will be upgraded"));
		part_name = "FW2";
	} else {
		LogInfo(("fw1 address will be upgraded"));
		part_name = "FW1";
	}

	uint32_t chksum = 0;
	int chklen = gNewImgLen - 4;    // skip 4byte ota length
	uint8_t *pTempbuf = pvPortMalloc(OTA_FILE_BLOCK_SIZE);

	void *chkfp = pfw_open(part_name, M_RAW | M_RDWR);
	if (!chkfp) {
		mainErr = OtaPalSignatureCheckFailed;
		goto exit;
	}
	pfw_seek(chkfp, 0, SEEK_SET);
	while (chklen > 0) {
		int rdlen = chklen > OTA_FILE_BLOCK_SIZE ? OTA_FILE_BLOCK_SIZE : chklen;
		pfw_read(chkfp, pTempbuf, rdlen);
		for (int i = 0; i < rdlen; i++) {
			if (chklen == (gNewImgLen - 4) && i < 8) {
				chksum += label_backup[i];
			} else {
				chksum += pTempbuf[i];
			}
		}
		chklen -= rdlen;
	}
	pfw_close(chkfp);

	printf("checksum Remote %x, Flash %x\n\r", file_checksum.u, chksum);
	if (file_checksum.u != chksum) {
		printf("checksum plaintext NOT correct!, Need to check here...\n\r");
		while (1);
	} else {
		printf("checksum plaintext correct!, Go to check the Signature of checksum...\n\r");
	}

	/* add checksum */
	CRYPTO_SignatureVerificationUpdate(pvContext, &chksum, 4);

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

	size_t lSignerCertSize;
	void *pvSigVerifyContext;
	uint8_t *pucSignerCert = NULL;

	while (true) {
		/* Verify an ECDSA-SHA256 signature. */
		if (CRYPTO_SignatureVerificationStart(&pvSigVerifyContext, cryptoASYMMETRIC_ALGORITHM_ECDSA,
											  cryptoHASH_ALGORITHM_SHA256) == pdFALSE) {
			mainErr = OtaPalSignatureCheckFailed;
			break;
		}

		LogInfo(("Started %s signature verification, file: %s\r\n", OTA_JsonFileSignatureKey, (const char *)C->pCertFilepath));

		pucSignerCert = prvPAL_ReadAndAssumeCertificate_amebaPro2((const uint8_t *const)C->pCertFilepath, &lSignerCertSize);
		if (pucSignerCert == NULL) {
			mainErr = OtaPalBadSignerCert;
			break;
		}

		mainErr = OTA_PAL_MAIN_ERR(prvSignatureVerificationUpdate_amebaPro2(C, pvSigVerifyContext));
		if (mainErr != OtaPalSuccess) {
			break;
		}
		if (CRYPTO_SignatureVerificationFinal(pvSigVerifyContext, (char *)pucSignerCert, lSignerCertSize,
											  C->pSignature->data, C->pSignature->size) == pdFALSE) {
			mainErr = OtaPalSignatureCheckFailed;
			prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateRejected);
		} else {
			mainErr = OtaPalSuccess;
		}
		break;
	}
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

	OTA_PRINT("[OTA] Authenticating and closing file.\r\n");

	if (C == NULL) {
		mainErr = OtaPalNullFileContext;
		goto exit;
	}

	/* close the fw file */
	if (C->pFile) {
		pfw_close(C->pFile);
	}

	if (C->pSignature != NULL) {
		LogInfo(("Authenticating and closing file."));
#if OTA_DEBUG && OTA_MEMDUMP
		vMemDump(C->pSignature->data, C->pSignature->size, "Signature");
#endif
		/* TODO: Verify the file signature, close the file and return the signature verification result. */
		mainErr = OTA_PAL_MAIN_ERR(prvPAL_CheckFileSignature_amebaPro2(C));

	} else {
		mainErr = OtaPalSignatureCheckFailed;
	}

	if (mainErr == OtaPalSuccess) {
		LogInfo(("%s signature verification passed.", OTA_JsonFileSignatureKey));
	} else {
		LogError(("Failed to pass %s signature verification: %d.", OTA_JsonFileSignatureKey, mainErr));

		/* If we fail to verify the file signature that means the image is not valid. We need to set the image state to aborted. */
		prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateAborted);
	}

exit:

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

int16_t prvPAL_WriteBlock_amebaPro2(OtaFileContext_t *C, uint32_t ulOffset, uint8_t *pData, uint32_t ulBlockSize)
{
	static uint32_t buf_size = 4096;
	static uint32_t ota_len = 0;
	static uint32_t total_blocks = 0;
	static uint32_t cur_block = 0;
	static uint32_t idx = 0;
	static uint32_t read_bytes = 0;
	static uint8_t *buf = NULL;

	if (C == NULL) {
		return -1;
	}

	OTA_PRINT("[OTA][%s] C->fileSize %d, iOffset: 0x%x: iBlockSize: 0x%x\r\n", __FUNCTION__, C->fileSize, ulOffset, ulBlockSize);

	// config parameter
	buf_size = OTA_FILE_BLOCK_SIZE;
	ota_len = C->fileSize;
	total_blocks = (ota_len + buf_size - 1) / buf_size;
	idx = ulOffset;
	read_bytes = ulBlockSize;
	cur_block = idx / buf_size;
	buf = pData;

	printf("[OTA WriteBlock] ota_len:%d, cur_block:%d\r\n", ota_len, cur_block);
#if OTA_DEBUG && OTA_MEMDUMP
	vMemDump(pData, ulBlockSize, "PAYLOAD0");
#endif

	if (idx >= ota_len) {
		OTA_PRINT("[OTA] image download is already done, dropped, idx=0x%X, ota_len=0x%X\r\n", idx, ota_len);
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
		OTA_PRINT("[OTA] FIRST image data arrived %d, back up the first 8-bytes fw label\r\n", read_bytes);
		memcpy(label_backup, buf, RTK_OTA_IMAGE_LABEL_LEN); // save 8-bytes fw label
#if 0
		memset(buf, 0xFF, RTK_OTA_IMAGE_LABEL_LEN); // not flash write 8-bytes fw label
#endif
		OTA_PRINT("[OTA] label_backup get [%llu]\r\n", label_backup);
		gNewImgLen = ota_len;
	}

	// check final block
	if (cur_block == (total_blocks - 1)) {
		read_bytes -= 4; // remove final 4 bytes checksum
		//memset(buf + read_bytes, 0xFF, buf_size - read_bytes);
		OTA_PRINT("[OTA] LAST image data arrived %d\r\n", read_bytes);
	}
	if (read_bytes <= 0) {
		goto exit;
	}

	// write block
	int wr_status = pfw_write(C->pFile, buf, read_bytes);
	if (wr_status < 0) {
		printf("[OTA WriteBlock][%s] ota flash failed\r\n", __FUNCTION__);
		return -1;
	}

exit:

	OTA_PRINT("[OTA] Write bytes: read_bytes %d, ulBlockSize %d\r\n", read_bytes, ulBlockSize);
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
		LogInfo(("fw2 need to be activated!"));
		part_name = "FW2";
	} else {
		LogInfo(("fw1 need to be activated!"));
		part_name = "FW1";
	}

#if 0
	// write back 8-bytes fw label
	void *fp = pfw_open(part_name, M_RAW | M_RDWR);
	if (!fp) {
		LogError(("Failed to open fw file system for OTA label"));
		mainErr = OtaPalActivateFailed;
		goto exit;
	}
	pfw_seek(fp, 0, SEEK_SET);
	int wr_status = pfw_write(fp, label_backup, RTK_OTA_IMAGE_LABEL_LEN);
	if (wr_status < 0) {
		printf("[%s] ota write label_backup failed \n\r", __FUNCTION__);
		mainErr = OtaPalActivateFailed;
		goto exit;
	}
	pfw_close(fp);
#endif

	/* set state to pending commit for self testing */
	prvPAL_SetPlatformImageState_amebaPro2(OtaImageStateTesting);

	/* reset device */
	OTA_PRINT("[OTA] Resetting MCU to activate new image.\r\n");
	vTaskDelay(100);
	prvSysReset_amebaPro2();
	/* should not run here */
	mainErr = OtaPalSuccess;

exit:

	return OTA_PAL_COMBINE_ERR(mainErr, 0);
}

OtaPalStatus_t prvPAL_ResetDevice_amebaPro2(void)
{
	prvSysReset_amebaPro2();
	return OTA_PAL_COMBINE_ERR(OtaPalSuccess, 0);
}

OtaPalStatus_t prvPAL_SetPlatformImageState_amebaPro2(OtaImageState_t eState)
{
	LogInfo(("%s", __func__));

	OtaPalMainStatus_t mainErr = OtaPalSuccess;
	OtaPalSubStatus_t subErr = 0;

	if ((eState != OtaImageStateUnknown) && (eState <= OtaLastImageState)) {
		/* write state to flash */
		int ret = ftl_common_write(RTK_OTA_IMAGE_STATE_FLASH_OFFSET, &eState, sizeof(OtaImageState_t));
		if (ret < 0) {
			OTA_PRINT("[%s] ftl write eState failed\n\r", __func__);
			mainErr = OtaPalBadImageState;
		}
	} else { /* Image state invalid. */
		LogError(("Invalid image state provided."));
		mainErr = OtaPalBadImageState;
	}

	return OTA_PAL_COMBINE_ERR(mainErr, subErr);
}

OtaPalImageState_t prvPAL_GetPlatformImageState_amebaPro2(void)
{
	OtaPalImageState_t eImageState = OtaPalImageStateUnknown;

	/* read the state from flash */
	OtaImageState_t eSavedAgentState = OtaImageStateUnknown;
	int ret = ftl_common_read(RTK_OTA_IMAGE_STATE_FLASH_OFFSET, &eSavedAgentState, sizeof(OtaImageState_t));
	if (ret < 0) {
		OTA_PRINT("[%s] ftl read eSavedAgentState failed\n\r", __func__);
		eImageState = OtaPalImageStateInvalid;
		goto exit;
	}

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
	LogInfo(("Image current state (0x%02x).", eImageState));

	return eImageState;
}

uint8_t *prvReadAndAssumeCertificate_amebaPro2(const uint8_t *const pucCertName, uint32_t *const lSignerCertSize)
{
	OTA_PRINT("prvReadAndAssumeCertificate: not implemented yet\r\n");
	return NULL;
}

