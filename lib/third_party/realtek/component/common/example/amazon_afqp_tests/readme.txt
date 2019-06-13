AMAZON FREERTOS AFQP TESTS ON IAR SDK EXAMPLE

Description:
Start to run Amazon FreeRTOS AFQP tests on IAR SDK on Ameba

Configuration:

1. Copy & paste below configurations to FreeRTOSConfig.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#define configTOTAL_HEAP_SIZE			( ( size_t ) ( 120 * 1024 ) ) //increase heap size for Amazon SDK need

#if (__IASMARM__ != 1)
#include "diag.h"
#include "platform_stdlib.h"
extern void cli(void);
#define configASSERT(x)			do { \
						if((x) == 0){ \
                                                 char *pcAssertTask = "ISR"; \
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
#define configLOGGING_MAX_MESSAGE_LENGTH            512

/* Set to 1 to prepend each log message with a message number, the task name,
 * and a time stamp. */
#define configLOGGING_INCLUDE_TIME_AND_TASK_NAME    1
#define configSUPPORT_STATIC_ALLOCATION              1
#define configUSE_MALLOC_FAILED_HOOK 1
#endif
////////////////////////////////////////////////////////////////////////////////////////////////////////

2. Enable Amazon FreeRTOS AFQP tests SDK example in platform_opts.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////
#define CONFIG_USE_POLARSSL     0
#define CONFIG_USE_MBEDTLS      1 //use mbedtls instead of polarssl
#define CONFIG_EXAMPLE_AMAZON_AFQP_TESTS 1
///////////////////////////////////////////////////////////////////////////////////////////////////////////

3. Configure aws_clientcredential.h and aws_clientcredential_keys.h
Refer to Section “Configure Your Project” in https://docs.aws.amazon.com/freertos/latest/userguide/getting_started_ti.html, which will have the instructions. 

4. In order to run each individual tests please enable the relevant macro in "aws_test_runner_config.h"
	a. Currently only tests required for AFQP have been ported, check the AFQP User guide to find out enabled tests.
	b. Due to memory issues only one test can be run at a time.
	c. For all AFQP tests please keep the macro "testrunnerAFQP_ENABLED" uncommented along with the respective test enabled except for OTA PAL tests.
	d. It is best to refer to AFQP developer guide for each individual test before running them in order to see if any additional setup is needed.

5. For certain AFQP tests the keys and certificates need to be modified to specific credentials made for testing, these credentials can be found as comments in the respective credential files
	a. For sake of simplicity all tests and demo code use the same credential files.
	b. In order to run the TCP tests, a new set of TCP credentials must be generated in order to sun some tests successfully. Refer to AFQP Developer Guide for more details.
	c. In order to run OTA PAL tests, the valid signature for OTA data must be un-commented in the file "aws_ota_codesigner_certificate.h"
	d. For each test it is best to refer to the AFQP developers guide regarding specific instructions needed to run that test.

Execution:
The example will run test cases defined in aws_test_runner_config.h.

