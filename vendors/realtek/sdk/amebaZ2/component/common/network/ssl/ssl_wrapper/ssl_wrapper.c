
#include "mbedtls/sha1.h"

void mbedtls_sha1_starts_ret(mbedtls_sha1_context *ctx)
{
    mbedtls_sha1_starts(ctx);
}

void mbedtls_sha1_update_ret(mbedtls_sha1_context *ctx, const unsigned char *input, size_t ilen)
{
    mbedtls_sha1_update(ctx, input, ilen);
}

void mbedtls_sha1_finish_ret(mbedtls_sha1_context *ctx, unsigned char output[20])
{
    mbedtls_sha1_finish(ctx, output);
}

//void mbedtls_sha1_ret(const unsigned char *input, size_t ilen, unsigned char output[20], int is224)
//{
//    mbedtls_sha1(input, ilen, output, is224);
//}

