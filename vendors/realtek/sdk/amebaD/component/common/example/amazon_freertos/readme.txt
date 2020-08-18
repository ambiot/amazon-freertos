AMAZON FREERTOS SDK EXAMPLE

Description:
Start to run Amazon FreeRTOS SDK on Ameba

Configuration:

1. Copy & paste below configurations to FreeRTOSConfig.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#if (__IASMARM__ != 1)
#include "diag.h"
extern void cli(void);
#define configASSERT(x)			do { \
						if((x) == 0){ \
                                                 char *pcAssertTask = "NoTsk"; \
                                                 if( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED ) \
                                                 { \
                                                     pcAssertTask = pcTaskGetName( NULL );\
                                                 } \
							DiagPrintf("\n\r[%s]Assert(" #x ") failed on line %d in file %s", pcAssertTask, __LINE__, __FILE__); \
						cli(); for(;;);}\
					} while(0)

/* Map the FreeRTOS printf() to the logging task printf. */
    /* The function that implements FreeRTOS printf style output, and the macro
     * that maps the configPRINTF() macros to that function. */
extern void vLoggingPrintf( const char * pcFormat, ... );
#define configPRINTF( X )    vLoggingPrintf X

/* Non-format version thread-safe print. */
extern void vLoggingPrint( const char * pcMessage );
#define configPRINT( X )     vLoggingPrint( X )

/* Map the logging task's printf to the board specific output function. */
#define configPRINT_STRING( x )    DiagPrintf( x )

/* Sets the length of the buffers into which logging messages are written - so
 * also defines the maximum length of each log message. */
#define configLOGGING_MAX_MESSAGE_LENGTH            128

/* Set to 1 to prepend each log message with a message number, the task name,
 * and a time stamp. */
#define configLOGGING_INCLUDE_TIME_AND_TASK_NAME    1
#define configSUPPORT_STATIC_ALLOCATION              1
#define configUSE_MALLOC_FAILED_HOOK 1
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////

2. Configure aws_clientcredential.h and aws_clientcredential_keys.h
Refer to Section “Configure Your Project” in https://docs.aws.amazon.com/freertos/latest/userguide/getting_started_ti.html, which will have the instructions. 

3. For AmebaD, need to modify the config in config_rsa.h 
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_SSL_ALPN
#define MBEDTLS_THREADING_ALT
#define MBEDTLS_ASN1_WRITE_C
//#define MBEDTLS_DEBUG_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_C
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_PK_WRITE_C
#define MBEDTLS_THREADING_C
#define MBEDTLS_SSL_MAX_CONTENT_LEN 8192

Execution:
The example will run demos defined in aws_demo_runner.c.

