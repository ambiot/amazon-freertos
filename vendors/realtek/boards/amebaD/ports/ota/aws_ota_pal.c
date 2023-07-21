/*
 * FreeRTOS OTA PAL V1.0.0
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

/* C Runtime includes. */
#include <stdio.h>
#include <stdlib.h>

/* FreeRTOS include. */
#include "FreeRTOS.h"
#include "aws_iot_ota_pal.h"
#include "aws_iot_ota_agent_internal.h"

/* Specify the OTA signature algorithm we support on this platform. */
const char cOTA_JSON_FileSignatureKey[ OTA_FILE_SIG_KEY_STR_MAX_LENGTH ] = "sig-sha256-ecdsa";   /* FIX ME. */


/* The static functions below (prvPAL_CheckFileSignature and prvPAL_ReadAndAssumeCertificate)
 * are optionally implemented. If these functions are implemented then please set the following macros in
 * aws_test_ota_config.h to 1:
 * otatestpalCHECK_FILE_SIGNATURE_SUPPORTED
 * otatestpalREAD_AND_ASSUME_CERTIFICATE_SUPPORTED
 */

/**
 * @brief Verify the signature of the specified file.
 *
 * This function should be implemented if signature verification is not offloaded
 * to non-volatile memory io functions.
 *
 * This function is called from prvPAL_Close().
 *
 * @param[in] C OTA file context information.
 *
 * @return Below are the valid return values for this function.
 * kOTA_Err_None if the signature verification passes.
 * kOTA_Err_SignatureCheckFailed if the signature verification fails.
 * kOTA_Err_BadSignerCert if the if the signature verification certificate cannot be read.
 *
 */
static OTA_Err_t prvPAL_CheckFileSignature( OTA_FileContext_t * const C );

/**
 * @brief Read the specified signer certificate from the filesystem into a local buffer.
 *
 * The allocated memory returned becomes the property of the caller who is responsible for freeing it.
 *
 * This function is called from prvPAL_CheckFileSignature(). It should be implemented if signature
 * verification is not offloaded to non-volatile memory io function.
 *
 * @param[in] pucCertName The file path of the certificate file.
 * @param[out] ulSignerCertSize The size of the certificate file read.
 *
 * @return A pointer to the signer certificate in the file system. NULL if the certificate cannot be read.
 * This returned pointer is the responsibility of the caller; if the memory is allocated the caller must free it.
 */
static uint8_t * prvPAL_ReadAndAssumeCertificate( const uint8_t * const pucCertName,
                                                  uint32_t * const ulSignerCertSize );

/*-----------------------------------------------------------*/

#if defined(CONFIG_PLATFORM_8711B)
OTA_Err_t prvPAL_Abort_rtl8710b(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CreateFileForRx_rtl8710b(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CloseFile_rtl8710b(OTA_FileContext_t *C);
int16_t prvPAL_WriteBlock_rtl8710b(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize);
OTA_Err_t prvPAL_ActivateNewImage_rtl8710b(void);
OTA_Err_t prvPAL_SetPlatformImageState_rtl8710b (OTA_ImageState_t eState);
OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_rtl8710b( void );
#elif defined(CONFIG_PLATFORM_8721D)
OTA_Err_t prvPAL_Abort_rtl8721d(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CreateFileForRx_rtl8721d(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CloseFile_rtl8721d(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CheckFileSignature_rtl8721d(OTA_FileContext_t * const C);
int32_t prvPAL_WriteBlock_rtl8721d(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize);
OTA_Err_t prvPAL_ActivateNewImage_rtl8721d(void);
OTA_Err_t prvPAL_SetPlatformImageState_rtl8721d (OTA_ImageState_t eState);
OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_rtl8721d( void );
#else
#error "This platform is not supported!"
#endif

OTA_Err_t prvPAL_CreateFileForRx( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CreateFileForRx" );
	return prvPAL_CreateFileForRx_rtl8721d(C) == pdTRUE?kOTA_Err_None:kOTA_Err_RxFileCreateFailed;
}
/*-----------------------------------------------------------*/

OTA_Err_t prvPAL_Abort( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_Abort" );
	return prvPAL_Abort_rtl8721d(C);
}
/*-----------------------------------------------------------*/

/* Write a block of data to the specified file. */
int32_t prvPAL_WriteBlock( OTA_FileContext_t * const C,
                           uint32_t ulOffset,
                           uint8_t * const pacData,
                           uint32_t ulBlockSize )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_WriteBlock" );
	return prvPAL_WriteBlock_rtl8721d(C, ulOffset, pacData, ulBlockSize);
}
/*-----------------------------------------------------------*/

OTA_Err_t prvPAL_CloseFile( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CloseFile" );
	return prvPAL_CloseFile_rtl8721d(C);
}
/*-----------------------------------------------------------*/


static OTA_Err_t prvPAL_CheckFileSignature( OTA_FileContext_t * const C )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_CheckFileSignature" );
    return prvPAL_CheckFileSignature_rtl8721d(C);
}
/*-----------------------------------------------------------*/

static uint8_t * prvPAL_ReadAndAssumeCertificate( const uint8_t * const pucCertName,
                                                  uint32_t * const ulSignerCertSize )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_ReadAndAssumeCertificate" );
    return (uint8_t *) prvPAL_ReadAndAssumeCertificate_rtl8721d( pucCertName, ulSignerCertSize);
}
/*-----------------------------------------------------------*/

OTA_Err_t prvPAL_ResetDevice( void )
{
    DEFINE_OTA_METHOD_NAME("prvPAL_ResetDevice");
	return prvPAL_ResetDevice_rtl8721d();
}
/*-----------------------------------------------------------*/

OTA_Err_t prvPAL_ActivateNewImage( void )
{
    DEFINE_OTA_METHOD_NAME("prvPAL_ActivateNewImage");
	return prvPAL_ActivateNewImage_rtl8721d();
}
/*-----------------------------------------------------------*/

OTA_Err_t prvPAL_SetPlatformImageState( OTA_ImageState_t eState )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_SetPlatformImageState" );
	return prvPAL_SetPlatformImageState_rtl8721d(eState);
}
/*-----------------------------------------------------------*/

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState( void )
{
    DEFINE_OTA_METHOD_NAME( "prvPAL_GetPlatformImageState" );
	return prvPAL_GetPlatformImageState_rtl8721d();
}
/*-----------------------------------------------------------*/

/* Provide access to private members for testing. */
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
    #include "aws_ota_pal_test_access_define.h"
#endif
