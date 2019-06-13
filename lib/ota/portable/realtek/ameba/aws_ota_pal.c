/*
 * Amazon FreeRTOS OTA PAL for Realtek Ameba V1.0.0
 * Copyright (C) 2018 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
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

/* OTA PAL implementation for Realtek Ameba platform. */

#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "aws_crypto.h"
#include "aws_ota_pal.h"
#include "aws_ota_agent.h"
#include "aws_ota_agent_internal.h"

/* Specify the OTA signature algorithm we support on this platform. */
const char cOTA_JSON_FileSignatureKey[ OTA_FILE_SIG_KEY_STR_MAX_LENGTH ] = "sig-sha256-ecdsa";

#if defined(CONFIG_PLATFORM_8711B)
OTA_Err_t prvPAL_Abort_rtl8710b(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CreateFileForRx_rtl8710b(OTA_FileContext_t *C);
OTA_Err_t prvPAL_CloseFile_rtl8710b(OTA_FileContext_t *C);
int16_t prvPAL_WriteBlock_rtl8710b(OTA_FileContext_t *C, int32_t iOffset, uint8_t* pacData, uint32_t iBlockSize);
OTA_Err_t prvPAL_ActivateNewImage_rtl8710b(void);
OTA_Err_t prvPAL_SetPlatformImageState_rtl8710b (OTA_ImageState_t eState);
OTA_PAL_ImageState_t prvPAL_GetPlatformImageState_rtl8710b( void );
#else
#error "This platform is not supported!"
#endif

OTA_Err_t prvPAL_CreateFileForRx( OTA_FileContext_t * const C )
{
    return prvPAL_CreateFileForRx_rtl8710b(C) == pdTRUE?kOTA_Err_None:kOTA_Err_RxFileCreateFailed;
}

OTA_Err_t prvPAL_Abort( OTA_FileContext_t * const C )
{
    return prvPAL_Abort_rtl8710b(C);
}

int16_t prvPAL_WriteBlock( OTA_FileContext_t * const C, uint32_t ulOffset, uint8_t * const pacData, uint32_t ulBlockSize )
{
    return prvPAL_WriteBlock_rtl8710b(C, ulOffset, pacData, ulBlockSize);
}

OTA_Err_t prvPAL_CloseFile( OTA_FileContext_t * const C )
{
    return prvPAL_CloseFile_rtl8710b(C);
}

OTA_Err_t prvPAL_ResetDevice( void )
{
    return prvPAL_ResetDevice_rtl8710b();
}

OTA_Err_t prvPAL_ActivateNewImage( void )
{
    return prvPAL_ActivateNewImage_rtl8710b();
}

OTA_Err_t prvPAL_SetPlatformImageState ( OTA_ImageState_t eState )
{
    return prvPAL_SetPlatformImageState_rtl8710b(eState);
}

OTA_PAL_ImageState_t prvPAL_GetPlatformImageState( void )
{
    return prvPAL_GetPlatformImageState_rtl8710b();
}

/*-----------------------------------------------------------*/

/* Provide access to private members for testing. */
#ifdef AMAZON_FREERTOS_ENABLE_UNIT_TESTS
#include "aws_ota_pal_test_access_define.h"
#endif
