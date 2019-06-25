#ifndef AWS_CLIENT_CREDENTIAL_KEYS_H
#define AWS_CLIENT_CREDENTIAL_KEYS_H

#include <stdint.h>

/*
 * PEM-encoded client certificate
 *
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----\n"
 */
#define keyCLIENT_CERTIFICATE_PEM       "-----BEGIN CERTIFICATE-----\n" \
"MIIDWjCCAkKgAwIBAgIVAPB5FIL0Ehbc3VAEwwwRwuZM1yE9MA0GCSqGSIb3DQEB\n" \
"CwUAME0xSzBJBgNVBAsMQkFtYXpvbiBXZWIgU2VydmljZXMgTz1BbWF6b24uY29t\n" \
"IEluYy4gTD1TZWF0dGxlIFNUPVdhc2hpbmd0b24gQz1VUzAeFw0xODA5MDQwODUw\n" \
"MDFaFw00OTEyMzEyMzU5NTlaMB4xHDAaBgNVBAMME0FXUyBJb1QgQ2VydGlmaWNh\n" \
"dGUwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDRWe0qXzKCGL3Evrkd\n" \
"Hb+ado7MhSdsDEXriKT/uTmnuiBpJs6qMu3AMHtJIkfFIv8/gUuLyTzB//rAyVlG\n" \
"h91b0dbYY4EQjR/ihCoEhCGsVp+fV9sV+t0/o60gQLFTk9/eX56bldgxG++IEg42\n" \
"Vw4Y+XU0x/34dDlj/KXkrrhlzT1vuun5jC0M/AFs1pdJCNwYIDjeXrFwuJ80QOmf\n" \
"DauvgtvmEafPAuA36xxYkgAfUTy/RkhC2ZGlbQINaRKNtrA1kFT9Y93qV20U4zUe\n" \
"j3LgkZNuhDIS7/rtYHEcoS6XtCEqxzGoclp4FN/4exS4JvOr3d47KJF4tWXlPMSs\n" \
"hfbvAgMBAAGjYDBeMB8GA1UdIwQYMBaAFCPY43WL72lKctFXyKJJa7dhLXRDMB0G\n" \
"A1UdDgQWBBSj4sWVdKdHrH5uWjMrsXFSWvtYijAMBgNVHRMBAf8EAjAAMA4GA1Ud\n" \
"DwEB/wQEAwIHgDANBgkqhkiG9w0BAQsFAAOCAQEAla0A/i1ZvrAHvQc+ym8oEZka\n" \
"3B02jggO4E2nZsM5jVK855yR92LuOz2ncGIRbsMqzgkWT/AHlupN7yU0EIwskdhE\n" \
"ZRFPw8CQZeCfWpeEF9QJR2HrX1t4qUBUPxqoUEoBVJnJwobfG9Im/Sdc0TSvv8Jr\n" \
"RO7Jsh9QCyf1WorUYVmiR0gn2JqQ+8qilLSs73d9OS+6l2YcWewPxrQgNfIXtzQs\n" \
"SGs2Yy54prgWHg/FXDtB0rLTegr8sVTysBA9wNRRj1C7ckrLMDgaH/wo+fpgb5rn\n" \
"nOqptHEqHSGes2ruq0PTaDgZJEKmxT2iyTru73YCFzFHMYQbH8kbbc1o1Se4Ew==\n" \
"-----END CERTIFICATE-----"
/*
 * PEM-encoded issuer certificate for AWS IoT Just In Time Registration (JITR).
 * This is required if you're using JITR, since the issuer (Certificate 
 * Authority) of the client certificate is used by the server for routing the 
 * device's initial request. (The device client certificate must always be 
 * sent as well.) For more information about JITR, see:
 *  https://docs.aws.amazon.com/iot/latest/developerguide/jit-provisioning.html, 
 *  https://aws.amazon.com/blogs/iot/just-in-time-registration-of-device-certificates-on-aws-iot/.
 *
 * If you're not using JITR, set below to NULL.
 * 
 * Must include the PEM header and footer:
 * "-----BEGIN CERTIFICATE-----\n"\
 * "...base64 data...\n"\
 * "-----END CERTIFICATE-----\n"
 */
#define keyJITR_DEVICE_CERTIFICATE_AUTHORITY_PEM  NULL

/*
 * PEM-encoded client private key.
 *
 * Must include the PEM header and footer:
 * "-----BEGIN RSA PRIVATE KEY-----\n"\
 * "...base64 data...\n"\
 * "-----END RSA PRIVATE KEY-----\n"
 */
#define keyCLIENT_PRIVATE_KEY_PEM       "-----BEGIN RSA PRIVATE KEY-----\n" \
"MIIEpQIBAAKCAQEA0VntKl8yghi9xL65HR2/mnaOzIUnbAxF64ik/7k5p7ogaSbO\n" \
"qjLtwDB7SSJHxSL/P4FLi8k8wf/6wMlZRofdW9HW2GOBEI0f4oQqBIQhrFafn1fb\n" \
"FfrdP6OtIECxU5Pf3l+em5XYMRvviBIONlcOGPl1NMf9+HQ5Y/yl5K64Zc09b7rp\n" \
"+YwtDPwBbNaXSQjcGCA43l6xcLifNEDpnw2rr4Lb5hGnzwLgN+scWJIAH1E8v0ZI\n" \
"QtmRpW0CDWkSjbawNZBU/WPd6ldtFOM1Ho9y4JGTboQyEu/67WBxHKEul7QhKscx\n" \
"qHJaeBTf+HsUuCbzq93eOyiReLVl5TzErIX27wIDAQABAoIBAFOnp4mi+L/buxj/\n" \
"TWFBHetPLMi0/IfEb7wqoiQ9k7heO81I4JRKPY7q0JjsxdkUnwJ1XAbDpy+uZjjb\n" \
"PH1elSm3tC5LtKs0eWU+grvDQOFoy+gUZ0tlLzva37dfonxPPsnRU8PAIN+e1hx9\n" \
"eohkSRHZvOgWcnnxl21I69Fxr+WdYI00aQ3tZbwpDyuol5oKGdj6tzWJOn6PS7ES\n" \
"mTjtjxZq5BnLSkiJ/BVPCWQ0oXSYr+sH/r7yv4C5Y4yuzTP1P4REKazf0WhAN9kb\n" \
"u269rwMCCQX+2YWlGSQ759S9J6aIUwuOs42CnRf5S3n4GcFBGjFJDGkLIFlT5rhj\n" \
"vniGbEECgYEA8z7QIDg6ke8LI1TpxVFNIdOikP8You5h4CDCkTqqamfCxdLz12eK\n" \
"4SIbY7UkmRo+TMofyX11tnM5789RVUSmEJZCyp7ETJr6MXQpkJ9Lr5fVnjYB8n9G\n" \
"bBmSvQowW6TOIHEf6N9O24YfMTWUIg59p15UC1mz+7hgPCbSozKdwLcCgYEA3FQj\n" \
"drgRF1QR1kQczjAuBgRRtBHJ/HI2HQjL3KOqnaOe0zQp21C1dWATEUAqwa9hfOwy\n" \
"Z2YBh7f/VjhwZ24IBrgYu+bM8U3wzWz/Wx4p8D6Yug8t6Qodi8Ou7fnB/ucDAEHU\n" \
"BouIMmXBkLL950VWSkkMkFkSlQEBNfrXjIjV04kCgYEAuk5d8o+erK++hC1BcBQ5\n" \
"U7NZTF1WbVTQOm5dGAcrB5y5nfNZOJ7hzFXnTJMtmOZ02TPm7NjfFANlWLwdu4V8\n" \
"/C9asP6xs0rwXnhubYrU2YUd5LPRAf9h3OIpdgkbyNWN22NJ4X9IUvODaJl1ADDB\n" \
"JTQmgxsNs7sgWnIR21WlZjkCgYEAy2bJjpIDBTxRczNSPG9yLY/TNZ+ujuWJW2iB\n" \
"e2GE4x7oQybG9Xce7gsRzz0ju8cDAuay6Y3cH8UXbiuQaYvE0R8nhmBeFu7TPXJA\n" \
"k4fXQ7kmGa5lvdvexuaSGZQXKhFuTdB3wssRoyUZe1Ii4Fy8ervRY3k3lGNnEMRQ\n" \
"Naclb7ECgYEAucru35dS6uv9kpdr9Ovl/ryrX81bfdWLOuAZm9MurFkSyLCgcIe9\n" \
"IMHqt9/cn2XPO28aAC3gg7E2MJ3Sj4UlPkW9FAT0ECmWjGukc4y4Mry1QS9KipDI\n" \
"CZDSJdLdvXBu0ySZmnN59oAac8Etm/SIV3fpnVRxcloxrxcBkweMHp4=\n" \
"-----END RSA PRIVATE KEY-----"

/* The constants above are set to const char * pointers defined in aws_dev_mode_key_provisioning.c,
 * and externed here for use in C files.  NOTE!  THIS IS DONE FOR CONVENIENCE
 * DURING AN EVALUATION PHASE AND IS NOT GOOD PRACTICE FOR PRODUCTION SYSTEMS 
 * WHICH MUST STORE KEYS SECURELY. */
extern const char clientcredentialCLIENT_CERTIFICATE_PEM[];
extern const char *clientcredentialJITR_DEVICE_CERTIFICATE_AUTHORITY_PEM;
extern const char clientcredentialCLIENT_PRIVATE_KEY_PEM[];
extern const uint32_t clientcredentialCLIENT_CERTIFICATE_LENGTH;
extern const uint32_t clientcredentialCLIENT_PRIVATE_KEY_LENGTH;

#endif /* AWS_CLIENT_CREDENTIAL_KEYS_H */
