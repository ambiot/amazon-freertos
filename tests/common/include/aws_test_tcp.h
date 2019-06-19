/*
 * Amazon FreeRTOS
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
#ifndef AWS_TEST_TCP_H
#define AWS_TEST_TCP_H

/* Non-Encrypted Echo Server.
 * Update tcptestECHO_SERVER_ADDR# and
 * tcptestECHO_PORT with IP address
 * and port of unencrypted TCP echo server. */
#define tcptestECHO_SERVER_ADDR0         192
#define tcptestECHO_SERVER_ADDR1         168
#define tcptestECHO_SERVER_ADDR2         1
#define tcptestECHO_SERVER_ADDR3         64
#define tcptestECHO_PORT                 ( 8883 )

/* Encrypted Echo Server.
 * If tcptestSECURE_SERVER is set to 1, the following must be updated:
 * 1. aws_clientcredential.h to use a valid AWS endpoint.
 * 2. aws_clientcredential_keys.h with corresponding AWS keys.
 * 3. tcptestECHO_SERVER_TLS_ADDR0-3 with the IP address of an
 * echo server using TLS.
 * 4. tcptestECHO_PORT_TLS, with the port number of the echo server
 * using TLS.
 * 5. tcptestECHO_HOST_ROOT_CA with the trusted root certificate of the
 * echo server using TLS. */
#define tcptestSECURE_SERVER             1

#define tcptestECHO_SERVER_TLS_ADDR0     192
#define tcptestECHO_SERVER_TLS_ADDR1     168
#define tcptestECHO_SERVER_TLS_ADDR2     1
#define tcptestECHO_SERVER_TLS_ADDR3     64
#define tcptestECHO_PORT_TLS             ( 9000 )

/* Number of times to retry a connection if it fails. */
#define tcptestRETRY_CONNECTION_TIMES    6

/* The root certificate used for the encrypted echo server.
 * This certificate is self-signed, and not in the trusted catalog. */
static const char tcptestECHO_HOST_ROOT_CA[] = \
 "-----BEGIN CERTIFICATE-----\n" \
"MIID5TCCAs2gAwIBAgIJAMLREgkGDi9vMA0GCSqGSIb3DQEBCwUAMIGIMQswCQYD\n" \
"VQQGEwJVUzELMAkGA1UECAwCV0ExDjAMBgNVBAcMBVBsYWNlMRQwEgYDVQQKDAtZ\n" \
"b3VyQ29tcGFueTELMAkGA1UECwwCSVQxFjAUBgNVBAMMDXd3dy55b3Vycy5jb20x\n" \
"ITAfBgkqhkiG9w0BCQEWEnlvdXJFbWFpbEB5b3VyLmNvbTAeFw0xOTAxMTgwNzA1\n" \
"NTFaFw0yMDAxMTgwNzA1NTFaMIGIMQswCQYDVQQGEwJVUzELMAkGA1UECAwCV0Ex\n" \
"DjAMBgNVBAcMBVBsYWNlMRQwEgYDVQQKDAtZb3VyQ29tcGFueTELMAkGA1UECwwC\n" \
"SVQxFjAUBgNVBAMMDXd3dy55b3Vycy5jb20xITAfBgkqhkiG9w0BCQEWEnlvdXJF\n" \
"bWFpbEB5b3VyLmNvbTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAM+x\n" \
"qJscTGQ6Oy3efspJtRZrGDR9bWzWienMR8tt+CdIdNp2wXBu06asjDoosPg2WVZG\n" \
"Gzyw1PV/w6oKtoOrNkVxTcr5Y5j6TyCZ0nUfkIqYIRAzPDQ391ngg2KZ/CaD0sKn\n" \
"9TFJMo6vLnrbbxpyEwI0JygjT+Ha1Uvk6E9asVO/PZsWHFoP8KMtdMY0DwP8qJ9n\n" \
"//n9qLcXLQDVd+ZTqHTDA60qZ/7Isa3bk7eNuKPZSVt+IUDpAS62D03XXM5Xzz1b\n" \
"nalEg93MkZXgC+ra9Uz2TkZ9j+LdKOCRfd+KsP42DxRwc+QdO6vjicl6Pg9ZVELy\n" \
"1WiMk6A31VXwA8FoymsCAwEAAaNQME4wHQYDVR0OBBYEFPU0TNWmOsJ+V3T5G+Wg\n" \
"fjz4/Ke8MB8GA1UdIwQYMBaAFPU0TNWmOsJ+V3T5G+Wgfjz4/Ke8MAwGA1UdEwQF\n" \
"MAMBAf8wDQYJKoZIhvcNAQELBQADggEBAFHYC82ziVeLAt4ZZBYXNYfdzCoYymSc\n" \
"Q2AEgOGkJqCpK1FTXzpxYi8Q/aDOYpLjm3qu1N2XipmMRyL/MmUzoiUPDKCHZnJW\n" \
"YtrauDmWP/gUYHiLgYU9aqHgcDy362v5SV1V8tN5nqDuRro4nPJWgvJVWEWJVCHU\n" \
"D3u3QWc1+lezJQXqBO2QssZlJmtmK/BeS/gPwDaPjrb//iEUPG0m0npnytEn5cAv\n" \
"OyQOjVBVnC+vcNy+V43q6rfqpD/UHnAhTpGR3bkms/Fc9gk3HHDHIfTj9pH9BN45\n" \
"3OQJXwLWrvUE6hHhqvOqVRB1I+eyJdd+argiS7tpmG1Zuxk5x1Agjyw=\n" \
"-----END CERTIFICATE-----"; 

#endif /* ifndef AWS_TEST_TCP_H */
