/*
 * FreeRTOS PKCS #11 PAL V1.0.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/**
 * @file core_pkcs11_pal.c
 * @brief Device specific helpers for PKCS11 Interface.
 */

/* FreeRTOS Includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "iot_crypto.h"
#include "core_pkcs11.h"
#include "core_pkcs11_config.h"
#include "core_pkcs11_pal.h"

/* C runtime includes. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* virtual file system */
#include "vfs.h"

#define PKCS11_PRIVATE_KEY_FILE_NAME        "lfs:/AwsDevicePrivateKey.dat"
#define PKCS11_PUBLIC_KEY_FILE_NAME         "lfs:/AwsDevicePublicKey.dat"
#define PKCS11_CERTIFICATE_FILE_NAME        "lfs:/AwsDeviceCertificate.dat"
#define PKCS11_CODE_SIGNING_KEEY_FILE_NAME  "lfs:/AwsCodeSigningKey.dat"

#define pkcs11OBJECT_CERTIFICATE_MAX_SIZE   4096

enum eObjectHandles
{
    eInvalidHandle = 0, /* According to PKCS #11 spec, 0 is never a valid object handle. */
    eAwsDevicePrivateKey = 1,
    eAwsDevicePublicKey,
    eAwsDeviceCertificate,
    eAwsCodeSigningKey
};

/* Converts a label to its respective filename and handle. */
void prvLabelToFilenameHandle( uint8_t *pcLabel,
                               char **pcFileName,
                               CK_OBJECT_HANDLE_PTR pHandle )
{
    if( pcLabel != NULL )
    {
        /* Translate from the PKCS#11 label to local storage file name. */
        if( 0 == memcmp( pcLabel,
                         &pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS,
                         sizeof( pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS ) ) )
        {
            *pcFileName = PKCS11_CERTIFICATE_FILE_NAME;
            *pHandle = eAwsDeviceCertificate;
        }
        else if( 0 == memcmp( pcLabel,
                              &pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS,
                              sizeof( pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS ) ) )
        {
        	*pcFileName = PKCS11_PRIVATE_KEY_FILE_NAME;
            *pHandle = eAwsDevicePrivateKey;
        }
        else if( 0 == memcmp( pcLabel,
                              &pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS,
                              sizeof( pkcs11configLABEL_DEVICE_PUBLIC_KEY_FOR_TLS ) ) )
        {
        	*pcFileName = PKCS11_PUBLIC_KEY_FILE_NAME;
            *pHandle = eAwsDevicePublicKey;
        }
        else if( 0 == memcmp( pcLabel,
                              &pkcs11configLABEL_CODE_VERIFICATION_KEY,
                              sizeof( pkcs11configLABEL_CODE_VERIFICATION_KEY ) ) )
        {
        	*pcFileName = PKCS11_CODE_SIGNING_KEEY_FILE_NAME;
            *pHandle = eAwsCodeSigningKey;
        }
        else
        {
            *pcFileName = 0;
            *pHandle = eInvalidHandle;
        }
    }
}

/**
 * @brief Initializes the PKCS #11 PAL.
 *
 * @return CKR_OK on success.
 * CKR_FUNCTION_FAILED on failure.
 */
CK_RV PKCS11_PAL_Initialize( void )
{
    CRYPTO_Init();

    // virtual file syetem init
    vfs_init(NULL);
    vfs_user_register("lfs", VFS_FATFS, VFS_INF_SD);

    return CKR_OK;
}

/**
* @brief Writes a file to local storage.
*
* Port-specific file write for crytographic information.
*
* @param[in] pxLabel       Label of the object to be saved.
* @param[in] pucData       Data buffer to be written to file
* @param[in] ulDataSize    Size (in bytes) of data to be saved.
*
* @return The file handle of the object that was stored.
*/
CK_OBJECT_HANDLE PKCS11_PAL_SaveObject( CK_ATTRIBUTE_PTR pxLabel,
                                        CK_BYTE_PTR pucData,
                                        CK_ULONG ulDataSize )
{
    CK_OBJECT_HANDLE xHandle = eInvalidHandle;
    char *pcFileName = NULL;

    /* Converts a label to its respective filename and handle. */
    prvLabelToFilenameHandle( pxLabel->pValue, &pcFileName, &xHandle );

    if(xHandle != eInvalidHandle){
        FILE *fp = fopen(pcFileName, "wb+");
        if (fp == NULL) {
            //printf("fail to open file.\r\n");
            return eInvalidHandle;
        }
        fwrite(pucData, ulDataSize, 1, fp);
        fclose(fp);
    }
    return xHandle;
}

/**
* @brief Translates a PKCS #11 label into an object handle.
*
* Port-specific object handle retrieval.
*
*
* @param[in] pxLabel         Pointer to the label of the object
*                           who's handle should be found.
* @param[in] usLength       The length of the label, in bytes.
*
* @return The object handle if operation was successful.
* Returns eInvalidHandle if unsuccessful.
*/
CK_OBJECT_HANDLE PKCS11_PAL_FindObject( CK_BYTE_PTR pxLabel,
                                        CK_ULONG usLength )
{
	/* Avoid compiler warnings about unused variables. */
	( void ) usLength;

    CK_OBJECT_HANDLE xHandle = eInvalidHandle;
    char *pcFileName = NULL;

    /* Converts a label to its respective filename and handle. */
    prvLabelToFilenameHandle( pxLabel, &pcFileName, &xHandle );

    /* Check if object exists/has been created before returning. */
    if(xHandle != eInvalidHandle) {
        FILE *fp = fopen(pcFileName, "r");
        if (fp) {
            fclose(fp);
        } else {
            //file doesn't exist
            xHandle = eInvalidHandle;
        }
    }

    return xHandle;
}

/**
* @brief Gets the value of an object in storage, by handle.
*
* Port-specific file access for cryptographic information.
*
* This call dynamically allocates the buffer which object value
* data is copied into.  PKCS11_PAL_GetObjectValueCleanup()
* should be called after each use to free the dynamically allocated
* buffer.
*
* @sa PKCS11_PAL_GetObjectValueCleanup
*
* @param[in] pcFileName    The name of the file to be read.
* @param[out] ppucData     Pointer to buffer for file data.
* @param[out] pulDataSize  Size (in bytes) of data located in file.
* @param[out] pIsPrivate   Boolean indicating if value is private (CK_TRUE)
*                          or exportable (CK_FALSE)
*
* @return CKR_OK if operation was successful.  CKR_KEY_HANDLE_INVALID if
* no such object handle was found, CKR_DEVICE_MEMORY if memory for
* buffer could not be allocated, CKR_FUNCTION_FAILED for device driver
* error.
*/
CK_RV PKCS11_PAL_GetObjectValue( CK_OBJECT_HANDLE xHandle,
                                      CK_BYTE_PTR * ppucData,
                                      CK_ULONG_PTR pulDataSize,
                                      CK_BBOOL * pIsPrivate )
{
    CK_RV xReturn = CKR_OK;
    char *pcFileName = NULL;

    switch(xHandle)
    {
		case eAwsDeviceCertificate:
			pcFileName = PKCS11_CERTIFICATE_FILE_NAME;
			*pIsPrivate = CK_FALSE;
			break;
		case eAwsDevicePrivateKey:
			pcFileName = PKCS11_PRIVATE_KEY_FILE_NAME;
			*pIsPrivate = CK_TRUE;
			break;
		case eAwsDevicePublicKey:
			pcFileName = PKCS11_PUBLIC_KEY_FILE_NAME;
			*pIsPrivate = CK_FALSE;
			break;
		case eAwsCodeSigningKey:
			pcFileName = PKCS11_CODE_SIGNING_KEEY_FILE_NAME;
			*pIsPrivate = CK_FALSE;
			break;
		default:
			xReturn = CKR_KEY_HANDLE_INVALID;
            goto exit;
    }
    
    if(xHandle != eInvalidHandle) {
        FILE *fp = fopen(pcFileName, "r+");
        if (fp == NULL) {
            //file not exist
            xReturn = CKR_KEY_HANDLE_INVALID;
            goto exit;
        }
        fseek(fp, 0, SEEK_END);
        *pulDataSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        *ppucData = pvPortMalloc(pkcs11OBJECT_CERTIFICATE_MAX_SIZE + 1);
        if( *ppucData == NULL ) {
            xReturn = CKR_DEVICE_MEMORY;
            goto exit;
        }
        fread(*ppucData, *pulDataSize, 1, fp);
        fclose(fp);
    }

exit:

    return xReturn;
}


/**
* @brief Cleanup after PKCS11_GetObjectValue().
*
* @param[in] pucData       The buffer to free.
*                          (*ppucData from PKCS11_PAL_GetObjectValue())
* @param[in] ulDataSize    The length of the buffer to free.
*                          (*pulDataSize from PKCS11_PAL_GetObjectValue())
*/
void PKCS11_PAL_GetObjectValueCleanup( CK_BYTE_PTR pucData,
                                       CK_ULONG ulDataSize )
{
    /* Unused parameters. */
	( void ) ulDataSize;

	if( pucData )
		vPortFree( pucData );
}

/*-----------------------------------------------------------*/
#if 0 //defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
extern int rtw_get_random_bytes(void* dst, u32 size);
int mbedtls_hardware_poll( void * data,
                           unsigned char * output,
                           size_t len,
                           size_t * olen )
{
    rtw_get_random_bytes(output, len);
    *olen = len;

    return 0;
}
#endif
