
/**************************************************/
//  AmebaPro2 AWS integration test                //
/**************************************************/

/* Standard includes. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* FreeRTOS includes */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "platform_stdlib.h"

/* RTK tls - mbedtls */
#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif
#include "mbedtls/platform.h"

/* test config includes */
#include "test_param_config.h"
#include "test_execution_config.h"

#include "transport_interface.h"
#include "transport_interface_test.h"
#include "mqtt_test.h"
#include "qualification_test.h"
#include "platform_function.h"
#include "ota_pal_test.h"

/* aws-iot library includes */
#include "iot_system_init.h"
#include "iot_logging_task.h"
#include "transport_secure_sockets.h"
#include "core_pkcs11_config.h"
#include "aws_dev_mode_key_provisioning.h"

/* Logging Task Defines. */
#define mainLOGGING_MESSAGE_QUEUE_LENGTH    15
#define mainLOGGING_TASK_STACK_SIZE         configMINIMAL_STACK_SIZE * 8

/* device key provisioning mode *
 * one time device provisioning */
#define AMEBA_PROVISIONING_MODE             1

/* enable TLS (default: 1) */
static int tls_enable = 1;

static int RunDeviceOtaDemo(void);

typedef struct NetworkCredentials {
	/**
	 * @brief To use ALPN, set this to a NULL-terminated list of supported
	 * protocols in decreasing order of preference.
	 *
	 * See [this link]
	 * (https://aws.amazon.com/blogs/iot/mqtt-with-tls-client-authentication-on-port-443-why-it-is-useful-and-how-it-works/)
	 * for more information.
	 */
	const char **pAlpnProtos;

	/**
	 * @brief Disable server name indication (SNI) for a TLS session.
	 */
	BaseType_t disableSni;

	const unsigned char *pRootCa;    /**< @brief String representing a trusted server root certificate. */
	size_t rootCaSize;               /**< @brief Size associated with #NetworkCredentials.pRootCa. */
	const unsigned char *pUserName;  /**< @brief String representing the username for MQTT. */
	size_t userNameSize;             /**< @brief Size associated with #NetworkCredentials.pUserName. */
	const unsigned char *pPassword;  /**< @brief String representing the password for MQTT. */
	size_t passwordSize;             /**< @brief Size associated with #NetworkCredentials.pPassword. */
	const char *pClientCertLabel;    /**< @brief String representing the PKCS #11 label for the client certificate. */
	const char *pPrivateKeyLabel;    /**< @brief String representing the PKCS #11 label for the private key. */
} NetworkCredentials_t;

struct NetworkContext {
	SecureSocketsTransportParams_t *pParams;
};

static NetworkContext_t xNetworkContext = { 0 };
static NetworkContext_t xSecondNetworkContext = { 0 };
static TransportInterface_t xTransport = { 0 };
static NetworkCredentials_t xNetworkCredentials = { 0 };
static SecureSocketsTransportParams_t secureSocketsTransportParams = { 0 };
static SecureSocketsTransportParams_t SecondsecureSocketsTransportParams = { 0 };

/* brief Transport timeout in milliseconds for transport send and receive. */
#define testonfigTRANSPORT_SEND_RECV_TIMEOUT_MS    ( 750 )

#include "lwip_netconf.h"
static bool isIpAddress(const char *address)
{
	struct sockaddr_in addr4 = {};
	int result4 = inet_pton(AF_INET, address, (void *)(&addr4));
	return (result4 == 1);
}

static NetworkConnectStatus_t prvTransportNetworkConnect(void *pNetworkContext,
		TestHostInfo_t *pHostInfo,
		void *pNetworkCredentials)
{
	/* Connect the transport network. */
	ServerInfo_t xServerInfo = { 0 };
	SocketsConfig_t xSocketsConfig = { 0 };
	TransportSocketStatus_t xNetworkStatus = TRANSPORT_SOCKET_STATUS_SUCCESS;

	/* Set the credentials for establishing a TLS connection. */
	/* Initializer server information. */
	xServerInfo.pHostName = pHostInfo->pHostName;
	xServerInfo.hostNameLength = strlen(pHostInfo->pHostName);
	xServerInfo.port = pHostInfo->port;

	//printf("pHostName, hostNameLength, port: %s, %d, %d\r\n", xServerInfo.pHostName, xServerInfo.hostNameLength, xServerInfo.port);

	/* Configure credentials for TLS mutual authenticated session (default) */
	xSocketsConfig.enableTls = true;
	xSocketsConfig.pAlpnProtos = NULL;
	xSocketsConfig.maxFragmentLength = 0;
	xSocketsConfig.disableSni = false;
	xSocketsConfig.pRootCa = ((NetworkCredentials_t *)pNetworkCredentials)->pRootCa;
	xSocketsConfig.rootCaSize = ((NetworkCredentials_t *)pNetworkCredentials)->rootCaSize;
	xSocketsConfig.sendTimeoutMs = testonfigTRANSPORT_SEND_RECV_TIMEOUT_MS;
	xSocketsConfig.recvTimeoutMs = testonfigTRANSPORT_SEND_RECV_TIMEOUT_MS;

	if (isIpAddress(pHostInfo->pHostName)) {
		/* SNI(Server Name Indication) needs to be disabled for an server which only has an IP address but no hostname. */
		printf("Disable SNI... Connect to server with its IP address...\r\n");
		xSocketsConfig.disableSni = true;
	}

	if (tls_enable == 0) {
		/* disable TLS for plain text test */
		xSocketsConfig.enableTls = false;
	}

	//printf("pRootCa, rootCaSize: %s, %d\r\n", xSocketsConfig.pRootCa, xSocketsConfig.rootCaSize);

	/* Attempt to create a mutually authenticated TLS connection. */
	xNetworkStatus = SecureSocketsTransport_Connect(pNetworkContext, &xServerInfo, &xSocketsConfig);
	if (xNetworkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS) {
		return NETWORK_CONNECT_FAILURE;
	}
	printf("TransportNetworkConnect SUCCESS!\r\n");

	return NETWORK_CONNECT_SUCCESS;
}

static void prvTransportNetworkDisconnect(void *pNetworkContext)
{
	/* Disconnect the transport network. */
	/* Close the network connection. */
	TransportSocketStatus_t xNetworkStatus = TRANSPORT_SOCKET_STATUS_SUCCESS;
	xNetworkStatus = SecureSocketsTransport_Disconnect(pNetworkContext);
	if (xNetworkStatus != TRANSPORT_SOCKET_STATUS_SUCCESS) {
		LogError(("SecureSocketsTransport_Disconnect() failed to close the network connection. "
				  "StatusCode=%d.", (int) xNetworkStatus));
	}
}

/*-----------------------------------------------------------*/

void FRTest_TimeDelay(uint32_t delayMs)
{
	vTaskDelay(delayMs / portTICK_PERIOD_MS);
}

/*-----------------------------------------------------------*/

typedef struct TaskParam {
	StaticSemaphore_t joinMutexBuffer;
	SemaphoreHandle_t joinMutexHandle;
	FRTestThreadFunction_t threadFunc;
	void *pParam;
	TaskHandle_t taskHandle;
} TaskParam_t;

static void ThreadWrapper(void *pParam)
{
	TaskParam_t *pTaskParam = (TaskParam_t *)pParam;

	if ((pTaskParam != NULL) && (pTaskParam->threadFunc != NULL) && (pTaskParam->joinMutexHandle != NULL)) {
		pTaskParam->threadFunc(pTaskParam->pParam);

		/* Give the mutex. */
		xSemaphoreGive(pTaskParam->joinMutexHandle);
	}

	vTaskDelete(NULL);
}

FRTestThreadHandle_t FRTest_ThreadCreate(FRTestThreadFunction_t threadFunc, void *pParam)
{
	if (threadFunc == NULL) {
		return NULL;
	}
	TaskParam_t *pTaskParam = NULL;
	FRTestThreadHandle_t threadHandle = NULL;
	BaseType_t xReturned;

	pTaskParam = malloc(sizeof(TaskParam_t));
	configASSERT(pTaskParam != NULL);

	pTaskParam->joinMutexHandle = xSemaphoreCreateBinaryStatic(&pTaskParam->joinMutexBuffer);
	configASSERT(pTaskParam->joinMutexHandle != NULL);

	pTaskParam->threadFunc = threadFunc;
	pTaskParam->pParam = pParam;

	xReturned = xTaskCreate(ThreadWrapper,    /* Task code. */
							"ThreadWrapper",  /* All tasks have same name. */
							8192,             /* Task stack size. */
							pTaskParam,       /* Where the task writes its result. */
							tskIDLE_PRIORITY, /* Task priority. */
							&pTaskParam->taskHandle);
	configASSERT(xReturned == pdPASS);

	threadHandle = pTaskParam;

	return threadHandle;
}

int FRTest_ThreadTimedJoin(FRTestThreadHandle_t threadHandle, uint32_t timeoutMs)
{
	if (threadHandle == NULL) {
		return -1;
	}
	TaskParam_t *pTaskParam = (TaskParam_t *)threadHandle;
	BaseType_t xReturned;
	int retValue = 0;

	/* Check the parameters. */
	configASSERT(pTaskParam != NULL);
	configASSERT(pTaskParam->joinMutexHandle != NULL);

	/* Wait for the thread. */
	xReturned = xSemaphoreTake(pTaskParam->joinMutexHandle, pdMS_TO_TICKS(timeoutMs));

	if (xReturned != pdTRUE) {
		printf("Waiting thread exist failed after %u %d. Task abort.", timeoutMs, xReturned);

		/* Return negative value to indicate error. */
		retValue = -1;

		/* There may be used after free. Assert here to indicate error. */
		configASSERT(false);
	}

	free(pTaskParam);

	return retValue;
}

void *FRTest_MemoryAlloc(size_t size)
{
	return pvPortMalloc(size);
}

void FRTest_MemoryFree(void *ptr)
{
	return vPortFree(ptr);
}

void SetupTransportTestParam(TransportTestParam_t *pTestParam)
{
	configASSERT(pTestParam != NULL);

	/* Setup the transport interface. */
	xTransport.send = SecureSocketsTransport_Send;
	xTransport.recv = SecureSocketsTransport_Recv;

	xNetworkCredentials.pRootCa = ECHO_SERVER_ROOT_CA;
	xNetworkCredentials.rootCaSize = sizeof(ECHO_SERVER_ROOT_CA);
	xNetworkCredentials.pClientCertLabel = pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS;
	xNetworkCredentials.pPrivateKeyLabel = pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS;
	xNetworkCredentials.disableSni = pdFALSE;

	pTestParam->pTransport = &xTransport;
	xNetworkContext.pParams = &secureSocketsTransportParams;
	pTestParam->pNetworkContext = &xNetworkContext;
	xSecondNetworkContext.pParams = &SecondsecureSocketsTransportParams;
	pTestParam->pSecondNetworkContext = &xSecondNetworkContext;

	pTestParam->pNetworkConnect = prvTransportNetworkConnect;
	pTestParam->pNetworkDisconnect = prvTransportNetworkDisconnect;
	pTestParam->pNetworkCredentials = &xNetworkCredentials;

#if (AMEBA_PROVISIONING_MODE == 0)
	/* Provision the device with iot-core certs and key */
	ProvisioningParams_t xParams;
	xParams.pucClientCertificate = (uint8_t *)TRANSPORT_CLIENT_CERTIFICATE;
	xParams.ulClientCertificateLength = sizeof(TRANSPORT_CLIENT_CERTIFICATE);
	xParams.pucClientPrivateKey = (uint8_t *)TRANSPORT_CLIENT_PRIVATE_KEY;
	xParams.ulClientPrivateKeyLength = sizeof(TRANSPORT_CLIENT_PRIVATE_KEY);
	xParams.ulJITPCertificateLength = 0; /* Do not provision JITP certificate. */
	xParams.pucJITPCertificate = NULL;
	vAlternateKeyProvisioning(&xParams);
#endif
}

void SetupMqttTestParam(MqttTestParam_t *pTestParam)
{
	configASSERT(pTestParam != NULL);

	/* Setup the transport interface. */
	xTransport.send = SecureSocketsTransport_Send;
	xTransport.recv = SecureSocketsTransport_Recv;

	xNetworkCredentials.pRootCa = IOT_CORE_ROOT_CA;
	xNetworkCredentials.rootCaSize = sizeof(IOT_CORE_ROOT_CA);
	xNetworkCredentials.pClientCertLabel = pkcs11configLABEL_DEVICE_CERTIFICATE_FOR_TLS;
	xNetworkCredentials.pPrivateKeyLabel = pkcs11configLABEL_DEVICE_PRIVATE_KEY_FOR_TLS;
	xNetworkCredentials.disableSni = pdFALSE;

	pTestParam->pTransport = &xTransport;
	xNetworkContext.pParams = &secureSocketsTransportParams;
	pTestParam->pNetworkContext = &xNetworkContext;
	xSecondNetworkContext.pParams = &SecondsecureSocketsTransportParams;
	pTestParam->pSecondNetworkContext = &xSecondNetworkContext;

	pTestParam->pNetworkConnect = prvTransportNetworkConnect;
	pTestParam->pNetworkDisconnect = prvTransportNetworkDisconnect;
	pTestParam->pNetworkCredentials = &xNetworkCredentials;

#if (AMEBA_PROVISIONING_MODE == 0)
	/* Provision the device with iot-core certs and key */
	ProvisioningParams_t xParams;
	xParams.pucClientCertificate = (uint8_t *)MQTT_CLIENT_CERTIFICATE;
	xParams.ulClientCertificateLength = sizeof(MQTT_CLIENT_CERTIFICATE);
	xParams.pucClientPrivateKey = (uint8_t *)MQTT_CLIENT_PRIVATE_KEY;
	xParams.ulClientPrivateKeyLength = sizeof(MQTT_CLIENT_PRIVATE_KEY);
	xParams.ulJITPCertificateLength = 0; /* Do not provision JITP certificate. */
	xParams.pucJITPCertificate = NULL;
	vAlternateKeyProvisioning(&xParams);
#endif
}

#include "ota_config.h"
void SetupOtaPalTestParam(OtaPalTestParam_t *pTestParam)
{
	pTestParam->pageSize = otaconfigFILE_BLOCK_SIZE;
}

/*-----------------------------------------------------------*/

/* RTK Amebapro2 wifi check */
#include "wifi_conf.h"
#include "lwip_netconf.h"
#define wifi_wait_time 500 //Here we wait 5 second to wiat the fast connect 
static void wifi_common_init(void)
{
	uint32_t wifi_wait_count = 0;

	while (!((wifi_get_join_status() == RTW_JOINSTATUS_SUCCESS) && (*(u32 *)LwIP_GetIP(0) != IP_ADDR_INVALID))) {
		vTaskDelay(10);
		wifi_wait_count++;
		if (wifi_wait_count == wifi_wait_time) {
			printf("\r\nuse ATW0, ATW1, ATWC to make wifi connection\r\n");
			printf("wait for wifi connection...\r\n");
		}
	}
}

static void prvWifiConnect(void)
{
	wifi_common_init();
}

/*-----------------------------------------------------------*/

#include "vfs.h"
static void prvPlatformInitialization(void)
{
#if defined(MBEDTLS_PLATFORM_C)
	mbedtls_platform_set_calloc_free(calloc, free);
#endif

	vfs_init(NULL);
	vfs_user_register("sd", VFS_FATFS, VFS_INF_SD);
}

/*-----------------------------------------------------------*/

int aws_integration_test_main(void)
{
	/* HW/SW initialization required by AmebaPro2 for testing */
	prvPlatformInitialization();

	/* Create tasks that are not dependent on the Wi-Fi being initialized. */
	xLoggingTaskInitialize(mainLOGGING_TASK_STACK_SIZE,
						   tskIDLE_PRIORITY,
						   mainLOGGING_MESSAGE_QUEUE_LENGTH);

	if (SYSTEM_Init() == pdPASS) {
		/* Connect to the Wi-Fi before running the tests. */
		prvWifiConnect();

#if (AMEBA_PROVISIONING_MODE == 1)
		/* Provision the device with AWS certificate and private key. */
		vDevModeKeyProvisioning();
#endif

		/* Start the test tasks. */
#if (OTA_E2E_TEST_ENABLED == 1)
		RunDeviceOtaDemo();
#endif
		RunQualificationTest();
	}

	return 0;
}

/*-----------------------------------------------------------*/

/* Include common demo header. */
#include "aws_demo.h"

extern int RunCoreMqttMutualAuthDemo(bool awsIotMqttMode,
									 const char *pIdentifier,
									 void *pNetworkServerInfo,
									 void *pNetworkCredentialInfo,
									 const IotNetworkInterface_t *pNetworkInterface);

int RunDeviceAdvisorDemo(void)
{
	BaseType_t xResult = pdPASS;
	xResult = RunCoreMqttMutualAuthDemo(0, NULL, NULL, NULL, NULL);

	return (xResult == pdPASS) ? 0 : -1;
}

extern int RunOtaCoreMqttDemo(bool xAwsIotMqttMode,
							  const char *pIdentifier,
							  void *pNetworkServerInfo,
							  void *pNetworkCredentialInfo,
							  const IotNetworkInterface_t *pxNetworkInterface);

static int RunDeviceOtaDemo(void)
{
	BaseType_t xResult = pdPASS;
	xResult = RunOtaCoreMqttDemo(0, NULL, NULL, NULL, NULL);

	return (xResult == pdPASS) ? 0 : -1;
}

