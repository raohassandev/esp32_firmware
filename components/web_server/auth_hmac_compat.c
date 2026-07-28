#include "auth_hmac_compat.h"

#include <stdint.h>

#include "psa/crypto.h"

int mbedtls_md_hmac(const mbedtls_md_info_t *md_info,
                    const unsigned char *key,
                    size_t key_length,
                    const unsigned char *input,
                    size_t input_length,
                    unsigned char *output)
{
    if (!md_info || !key || key_length == 0U || !input || !output ||
        mbedtls_md_get_type(md_info) != MBEDTLS_MD_SHA256) {
        return -1;
    }

    if (psa_crypto_init() != PSA_SUCCESS) return -1;

    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, key_length * 8U);

    psa_key_id_t key_id = 0;
    psa_status_t status = psa_import_key(&attributes, key, key_length, &key_id);
    size_t output_length = 0;
    if (status == PSA_SUCCESS) {
        status = psa_mac_compute(key_id,
                                 PSA_ALG_HMAC(PSA_ALG_SHA_256),
                                 input,
                                 input_length,
                                 output,
                                 32U,
                                 &output_length);
    }
    if (key_id != 0) {
        psa_status_t destroy_status = psa_destroy_key(key_id);
        if (status == PSA_SUCCESS && destroy_status != PSA_SUCCESS) {
            status = destroy_status;
        }
    }
    psa_reset_key_attributes(&attributes);
    return status == PSA_SUCCESS && output_length == 32U ? 0 : -1;
}
