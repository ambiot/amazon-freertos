#include <platform_opts.h>

#if 1//defined(CONFIG_EXAMPLE_SSL_DOWNLOAD) && (CONFIG_EXAMPLE_SSL_DOWNLOAD == 1)

#include <FreeRTOS.h>
#include <task.h>
#include <platform/platform_stdlib.h>

#include "platform_opts.h"

#if CONFIG_USE_POLARSSL

#include <lwip/sockets.h>
#include <polarssl/config.h>
#include <polarssl/memory.h>
#include <polarssl/ssl.h>

#define SERVER_HOST    "176.34.62.248"
#define SERVER_PORT    443
#define RESOURCE       "/repository/IOT/Project_Cloud_A.bin"
#define BUFFER_SIZE    2048

static int my_random(void *p_rng, unsigned char *output, size_t output_len)
{
	rtw_get_random_bytes(output, output_len);
	return 0;
}

static void example_ssl_download_thread(void *param)
{
	int server_fd = -1, ret;
	struct sockaddr_in server_addr;
	ssl_context ssl;

	// Delay to wait for IP by DHCP
	vTaskDelay(10000);
	printf("\nExample: SSL download\n");

	memory_set_own(pvPortMalloc, vPortFree);
	memset(&ssl, 0, sizeof(ssl_context));

	if((ret = net_connect(&server_fd, SERVER_HOST, SERVER_PORT)) != 0) {
		printf("ERROR: net_connect ret(%d)\n", ret);
		goto exit;
	}

	if((ret = ssl_init(&ssl)) != 0) {
		printf("ERRPR: ssl_init ret(%d)\n", ret);
		goto exit;
	}

	ssl_set_endpoint(&ssl, SSL_IS_CLIENT);
	ssl_set_authmode(&ssl, SSL_VERIFY_NONE);
	ssl_set_rng(&ssl, my_random, NULL);
	ssl_set_bio(&ssl, net_recv, &server_fd, net_send, &server_fd);

	if((ret = ssl_handshake(&ssl)) != 0) {
		printf("ERROR: ssl_handshake ret(-0x%x)", -ret);
		goto exit;
	}
	else {
		unsigned char buf[BUFFER_SIZE + 1];
		int pos = 0, read_size = 0, resource_size = 0, content_len = 0, header_removed = 0;

		printf("SSL ciphersuite %s\n", ssl_get_ciphersuite(&ssl));
		sprintf(buf, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", RESOURCE, SERVER_HOST);
		ssl_write(&ssl, buf, strlen(buf));

		while((read_size = ssl_read(&ssl, buf + pos, BUFFER_SIZE - pos)) > 0) {
			if(header_removed == 0) {
				char *header = NULL;

				pos += read_size;
				buf[pos] = 0;
				header = strstr(buf, "\r\n\r\n");

				if(header) {
					char *body, *content_len_pos;

					body = header + strlen("\r\n\r\n");
					*(body - 2) = 0;
					header_removed = 1;
					printf("\nHTTP Header: %s\n", buf);

					// Remove header size to get first read size of data from body head
					read_size = pos - ((unsigned char *) body - buf);
					pos = 0;

					content_len_pos = strstr(buf, "Content-Length: ");
					if(content_len_pos) {
						content_len_pos += strlen("Content-Length: ");
						*(char*)(strstr(content_len_pos, "\r\n")) = 0;
						content_len = atoi(content_len_pos);
					}
				}
				else {
					if(pos >= BUFFER_SIZE){
						printf("ERROR: HTTP header\n");
						goto exit;
					}

					continue;
				}
			}

			printf("read resource %d bytes\n", read_size);
			resource_size += read_size;
		}

		printf("exit read. ret = %d\n", read_size);
		printf("http content-length = %d bytes, download resource size = %d bytes\n", content_len, resource_size);
	}

exit:
	if(server_fd >= 0)
		net_close(server_fd);

	ssl_free(&ssl);
	vTaskDelete(NULL);
}

void example_ssl_download(void)
{
	if(xTaskCreate(example_ssl_download_thread, ((const char*)"example_ssl_download_thread"), 2048, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(init_thread) failed", __FUNCTION__);
}

#elif CONFIG_USE_MBEDTLS /* CONFIG_USE_POLARSSL */

#include <mbedTLS/config.h>
#include <mbedTLS/platform.h>
#include <mbedtls/net_sockets.h>
#include <mbedTLS/ssl.h>
#include "device_lock.h"

//#define SERVER_HOST    "192.168.10.103"
char server_host[32] = {0};
#define DEFAULT_SERVER_HOST "192.168.1.100"
#define SERVER_HOST    server_host

#define SERVER_PORT    "443"
#define RESOURCE       "/ota.bin"
#define BUFFER_SIZE    2048

extern int NS_ENTRY secure_mbedtls_platform_set_calloc_free(void);
extern int NS_ENTRY secure_mbedtls_platform_set_ns_calloc_free(void* (*calloc_func)(size_t, size_t), void (*free_func)(void *));
extern void NS_ENTRY secure_mbedtls_ssl_init(mbedtls_ssl_context *ssl);
extern void NS_ENTRY secure_mbedtls_ssl_free(mbedtls_ssl_context *ssl);
extern void NS_ENTRY secure_mbedtls_ssl_conf_rng(mbedtls_ssl_config *conf, void *p_rng);
extern void NS_ENTRY secure_mbedtls_ssl_conf_dbg(mbedtls_ssl_config *conf, void  *p_dbg);
extern void NS_ENTRY secure_mbedtls_ssl_config_free(mbedtls_ssl_config *conf);
extern int NS_ENTRY secure_mbedtls_ssl_setup(mbedtls_ssl_context *ssl, const mbedtls_ssl_config *conf);
extern int NS_ENTRY secure_mbedtls_ssl_handshake(mbedtls_ssl_context *ssl);
extern char* NS_ENTRY secure_mbedtls_ssl_get_ciphersuite(const mbedtls_ssl_context *ssl, char *buf);
extern int NS_ENTRY secure_mbedtls_ssl_read(mbedtls_ssl_context *ssl, unsigned char *buf, size_t len);
extern int NS_ENTRY secure_mbedtls_ssl_write(mbedtls_ssl_context *ssl, const unsigned char *buf, size_t len);
extern int NS_ENTRY secure_mbedtls_ssl_close_notify(mbedtls_ssl_context *ssl);

extern mbedtls_pk_context* NS_ENTRY secure_mbedtls_pk_parse_key(void);
extern void NS_ENTRY secure_mbedtls_pk_free(mbedtls_pk_context *pk);
extern mbedtls_x509_crt* NS_ENTRY secure_mbedtls_x509_crt_parse(void);
extern mbedtls_x509_crt* NS_ENTRY secure_mbedtls_x509_crt_parse_ca(void);
extern void NS_ENTRY secure_mbedtls_x509_crt_free(mbedtls_x509_crt *crt);
extern int NS_ENTRY secure_mbedtls_ssl_conf_own_cert(mbedtls_ssl_config *conf, mbedtls_x509_crt *own_cert, mbedtls_pk_context *pk_key);
extern void NS_ENTRY secure_mbedtls_ssl_conf_verify(mbedtls_ssl_config *conf, void *p_vrfy);

static mbedtls_x509_crt* _cli_crt = NULL;
static mbedtls_pk_context* _clikey = NULL;
static mbedtls_x509_crt* _ca_crt = NULL;

static void* my_calloc(size_t nelements, size_t elementSize)
{
	size_t size;
	void *ptr = NULL;

	size = nelements * elementSize;
	ptr = pvPortMalloc(size);

	if(ptr)
		memset(ptr, 0, size);

	return ptr;
}

static void example_ssl_download_thread(void *param)
{
	int ret;
	mbedtls_net_context server_fd;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;

	// Delay to wait for IP by DHCP
//	vTaskDelay(10000);
	printf("\nExample: SSL download\n");

	rtw_create_secure_context(2048*4);
	secure_mbedtls_platform_set_calloc_free();
	secure_mbedtls_platform_set_ns_calloc_free(my_calloc, vPortFree);
	secure_set_ns_device_lock(device_mutex_lock, device_mutex_unlock);

	mbedtls_net_init(&server_fd);
	secure_mbedtls_ssl_init(&ssl);
	mbedtls_ssl_config_init(&conf);

	if((ret = mbedtls_net_connect(&server_fd, SERVER_HOST, SERVER_PORT, MBEDTLS_NET_PROTO_TCP)) != 0) {
		printf("ERROR: mbedtls_net_connect ret(%d)\n", ret);
		goto exit;
	}

	mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	if((ret = mbedtls_ssl_config_defaults(&conf,
			MBEDTLS_SSL_IS_CLIENT,
			MBEDTLS_SSL_TRANSPORT_STREAM,
			MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {

		printf("ERRPR: mbedtls_ssl_config_defaults ret(%d)\n", ret);
		goto exit;
	}

	_cli_crt = secure_mbedtls_x509_crt_parse();
	if(_cli_crt == NULL)
		goto exit;

	_clikey = secure_mbedtls_pk_parse_key();
	if(_clikey == NULL)
		goto exit;

	_ca_crt = secure_mbedtls_x509_crt_parse_ca();
	if(_ca_crt == NULL)
		goto exit;

	secure_mbedtls_ssl_conf_own_cert(&conf, _cli_crt, _clikey);
	mbedtls_ssl_conf_ca_chain(&conf, _ca_crt, NULL);
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
	secure_mbedtls_ssl_conf_rng(&conf, NULL);
	secure_mbedtls_ssl_conf_verify(&conf, NULL);

	if((ret = secure_mbedtls_ssl_setup(&ssl, &conf)) != 0) {
		printf("ERRPR: mbedtls_ssl_setup ret(%d)\n", ret);
		goto exit;
	}

	uint32_t start_time, end_time;
	start_time = xTaskGetTickCount();
	if((ret = secure_mbedtls_ssl_handshake(&ssl)) != 0) {
		printf("ERROR: mbedtls_ssl_handshake ret(-0x%x)", -ret);
		goto exit;
	}
	else {
		end_time = xTaskGetTickCount();
		printf("\n\r secure_mbedtls_ssl_handshake %d ms \n\r", end_time - start_time);

		unsigned char buf[BUFFER_SIZE + 1];
		int pos = 0, read_size = 0, resource_size = 0, content_len = 0, header_removed = 0;

		char ciphersuite_name_buf[100];
		printf("SSL ciphersuite %s\n", secure_mbedtls_ssl_get_ciphersuite(&ssl, ciphersuite_name_buf));
		sprintf(buf, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", RESOURCE, SERVER_HOST);
		secure_mbedtls_ssl_write(&ssl, buf, strlen(buf));

		start_time = xTaskGetTickCount();
		while(1) {
			if((read_size = secure_mbedtls_ssl_read(&ssl, buf + pos, BUFFER_SIZE - pos)) <= 0) {
				break;
			}

			if(header_removed == 0) {
				char *header = NULL;

				pos += read_size;
				buf[pos] = 0;
				header = strstr(buf, "\r\n\r\n");

				if(header) {
					char *body, *content_len_pos;

					body = header + strlen("\r\n\r\n");
					*(body - 2) = 0;
					header_removed = 1;
					printf("\nHTTP Header: %s\n", buf);

					// Remove header size to get first read size of data from body head
					read_size = pos - ((unsigned char *) body - buf);
					pos = 0;

					content_len_pos = strstr(buf, "Content-Length: ");
					if(content_len_pos) {
						content_len_pos += strlen("Content-Length: ");
						*(strstr(content_len_pos, "\r\n")) = 0;
						content_len = atoi(content_len_pos);
					}
				}
				else {
					if(pos >= BUFFER_SIZE){
						printf("ERROR: HTTP header\n");
						goto exit;
					}

					continue;
				}
			}

//			printf("read resource %d bytes\n", read_size);
			resource_size += read_size;

			if(resource_size >= content_len)
				break;
		}
		end_time = xTaskGetTickCount();
		printf("\n\r secure_mbedtls_ssl_read %d ms, %f Mbits/sec\n\r", end_time - start_time, (float) resource_size * 8 / (end_time - start_time) * 1000 / 1024 / 1024);

		printf("exit read. ret = %d\n", read_size);
		printf("http content-length = %d bytes, download resource size = %d bytes\n", content_len, resource_size);
	}

exit:
	mbedtls_net_free(&server_fd);
	secure_mbedtls_ssl_free(&ssl);
	secure_mbedtls_ssl_config_free(&conf);

	if(_cli_crt) {
		secure_mbedtls_x509_crt_free(_cli_crt);
		_cli_crt = NULL;
	}

	if(_clikey) {
		secure_mbedtls_pk_free(_clikey);
		_clikey = NULL;
	}

	if(_ca_crt) {
		secure_mbedtls_x509_crt_free(_ca_crt);
		_ca_crt = NULL;
	}

	vTaskDelete(NULL);
}

void example_ssl_download(void)
{
	// started by platform_opts.h
	if(strlen(server_host) == 0) {
		vTaskDelay(10000);
		strcpy(server_host, DEFAULT_SERVER_HOST);
	}

	if(xTaskCreate(example_ssl_download_thread, ((const char*)"example_ssl_download_thread"), 2048, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS)
		printf("\n\r%s xTaskCreate(init_thread) failed", __FUNCTION__);
}

void fATWl(void *arg)
{
	if(arg == NULL) {
		//strcpy(server_host, DEFAULT_SERVER_HOST);
		printf("\n\r ERROR: ATWl=server_ip \n\r");
		return;
	}
	else
		strcpy(server_host, (char *) arg);

	example_ssl_download();
}

#endif /* CONFIG_USE_POLARSSL */

#endif /*CONFIG_EXAMPLE_SSL_DOWNLOAD*/
