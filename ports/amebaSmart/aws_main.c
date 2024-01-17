/*
 * FreeRTOS V1.4.7
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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "platform_stdlib.h"

/* Demo includes */
#include "aws_demo.h"

/* AWS library includes. */
#include "iot_logging_task.h"
#include "iot_wifi.h"
#include "aws_clientcredential.h"
#include "aws_dev_mode_key_provisioning.h"

/* Logging Task Defines. */
#define mainLOGGING_MESSAGE_QUEUE_LENGTH    ( 15 )
#define mainLOGGING_TASK_STACK_SIZE         ( configMINIMAL_STACK_SIZE * 8 )

extern int RunCoreMqttMutualAuthDemo( bool awsIotMqttMode,
                                      const char * pIdentifier,
                                      void * pNetworkServerInfo,
                                      void * pNetworkCredentialInfo,
                                      const IotNetworkInterface_t * pNetworkInterface );

extern int RunDeviceShadowDemo( bool awsIotMqttMode,
                         const char * pIdentifier,
                         void * pNetworkServerInfo,
                         void * pNetworkCredentialInfo,
                         const void * pNetworkInterface );

extern int RunDeviceDefenderDemo( bool awsIotMqttMode,
                           const char * pIdentifier,
                           void * pNetworkServerInfo,
                           void * pNetworkCredentialInfo,
                           const void * pNetworkInterface );


extern int RunOtaCoreMqttDemo( bool xAwsIotMqttMode,
                               const char * pIdentifier,
                               void * pNetworkServerInfo,
                               void * pNetworkCredentialInfo,
                               const IotNetworkInterface_t * pxNetworkInterface );
/*-----------------------------------------------------------*/
/**
 * @brief Application runtime entry point.
 */
int aws_main( void )
{
    /* Create tasks that are not dependent on the Wi-Fi being initialized. */
    xLoggingTaskInitialize( mainLOGGING_TASK_STACK_SIZE,
                            tskIDLE_PRIORITY+6,
                            mainLOGGING_MESSAGE_QUEUE_LENGTH );

    CRYPTO_ConfigureThreading();

    RunCoreMqttMutualAuthDemo(0, NULL, NULL, NULL, NULL);
    //RunDeviceShadowDemo(0, NULL, NULL, NULL, NULL);
    //RunDeviceDefenderDemo(0, NULL, NULL, NULL, NULL);
    //RunOtaCoreMqttDemo(0, NULL, NULL, NULL, NULL);

    return 0;
}
/*-----------------------------------------------------------*/
