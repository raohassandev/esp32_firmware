#pragma once

#include <stddef.h>

#include "mbedtls/md.h"

/* Mbed TLS 4 removes HMAC operations from the MD API. The Engineering
 * authentication implementation uses this project-local compatibility entry
 * point, implemented through PSA Crypto, while retaining one call signature
 * across supported ESP-IDF toolchains. */
int mbedtls_md_hmac(const mbedtls_md_info_t *md_info,
                    const unsigned char *key,
                    size_t key_length,
                    const unsigned char *input,
                    size_t input_length,
                    unsigned char *output);
