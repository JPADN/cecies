/*
   Copyright 2020 Raphael Beck

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include <mbedtls/gcm.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/base64.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md_internal.h>

#include <ccrush.h>

#include "cecies/util.h"
#include "cecies/decrypt.h"

#include "cecies/data.txt"

/*
 * This avoids code duplication between the Curve25519 and Curve448 decryption variants (only key length and a few minor things differ).
 * The last "curve" argument determines which curve to use for decryption: pass 0 for Curve25519 and 1 for Curve448!
 */
static int cecies_decrypt(const uint8_t* encrypted_data, const size_t encrypted_data_length, const int encrypted_data_base64, char* private_key, uint8_t** output, size_t* output_length, const int curve)
{
    /* -------------------------------- Modified -------------------------------- */
    // Removed constraint... (it was expecting a salt)
    const size_t min_data_len = curve == 0 ? 0 : 0;
    /* ------------------------------ End modified ------------------------------ */
    const size_t key_length = curve == 0 ? CECIES_X25519_KEY_SIZE : CECIES_X448_KEY_SIZE;

    if (encrypted_data == NULL || output == NULL || output_length == NULL || private_key == NULL)
    {
        cecies_fprintf(stderr, "CECIES: decryption failed: one or more NULL arguments.\n");
        return CECIES_DECRYPT_ERROR_CODE_NULL_ARG;
    }

    if (encrypted_data_length < min_data_len)
    {
        cecies_fprintf(stderr, "CECIES: decryption failed: one or more invalid arguments.\n");
        return CECIES_DECRYPT_ERROR_CODE_INVALID_ARG;
    }

    int ret = 1;
    uint8_t* input = (uint8_t*)encrypted_data;
    size_t input_length = encrypted_data_length;

    if (encrypted_data_base64)
    {
        input = malloc(input_length);
        if (input == NULL)
        {
            cecies_fprintf(stderr, "CECIES: decryption failed: OUT OF MEMORY!\n");
            return CECIES_DECRYPT_ERROR_CODE_OUT_OF_MEMORY;
        }

        if (encrypted_data[input_length - 1] == '\0')
        {
            input_length--;
        }

        ret = mbedtls_base64_decode(input, input_length, &input_length, encrypted_data, input_length);
        if (ret != 0)
        {
            free(input);
            cecies_fprintf(stderr, "CECIES: decryption failed: couldn't base64-decode the given data! mbedtls_base64_decode returned %d\n", ret);
            return CECIES_DECRYPT_ERROR_CODE_INVALID_ARG;
        }
    }

    /* -------------------------------- Modified -------------------------------- */
    size_t olen = input_length - 8 - key_length - 8;
    /* ------------------------------ End Modified ------------------------------ */
    // size_t olen = input_length - 16 - 32 - key_length - 16;
    
    uint8_t iv[16] = { 0x00 };
    uint8_t tag[16] = { 0x00 };
    uint8_t salt[32] = { 0x00 };
    uint8_t aes_key[32] = { 0x00 };
    uint8_t R_bytes[64] = { 0x00 };
    uint8_t S_bytes[64] = { 0x00 };
    uint8_t private_key_bytes[64] = { 0x00 };

    size_t private_key_bytes_length = 0, S_bytes_length = 0;

    mbedtls_ecp_group ecp_group;
    // TODO
    /* -------------------------------- Modified -------------------------------- */
    mbedtls_aes_context aes_ctx;
    // mbedtls_gcm_context aes_ctx;
    mbedtls_md_context_t md_ctx;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;

    // Variables named after the ECIES illustration on https://asecuritysite.com/encryption/go_ecies
    mbedtls_mpi dA;
    mbedtls_ecp_point R;
    mbedtls_ecp_point S;

    mbedtls_ecp_group_init(&ecp_group);
    // TODO:
    /* -------------------------------- Modified -------------------------------- */
    mbedtls_aes_init(&aes_ctx);
    // mbedtls_gcm_init(&aes_ctx);
    mbedtls_md_init(&md_ctx);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_mpi_init(&dA);
    mbedtls_ecp_point_init(&R);
    mbedtls_ecp_point_init(&S);

    ret = mbedtls_ecp_group_load(&ecp_group, curve == 0 ? MBEDTLS_ECP_DP_CURVE25519 : MBEDTLS_ECP_DP_CURVE448);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: MbedTLS ECP group setup failed! mbedtls_ecp_group_load returned %d\n", ret);
        goto exit;
    }

    uint8_t pers[256];
    cecies_dev_urandom(pers, 128);
    snprintf((char*)(pers + 128), 128, "cecies_PERS_3~£,@+14/\\%llu", cecies_get_random_big_integer());
    mbedtls_sha512_ret(pers + 128, 128, pers + 128 + 64, 0);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, pers, CECIES_MIN(sizeof(pers), (MBEDTLS_CTR_DRBG_MAX_SEED_INPUT - MBEDTLS_CTR_DRBG_ENTROPY_LEN - 1)));
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: MbedTLS PRNG seed failed! mbedtls_ctr_drbg_seed returned %d\n", ret);
        goto exit;
    }

  /* -------------------------------- Modified -------------------------------- */
    memcpy(iv, input, 8);
    memcpy(iv+8, input + 40, 8);
    memcpy(R_bytes, input + 8, key_length);
    // memcpy(tag, input + 16 + key_length, 16);

    // salt == 0
    /* ------------------------------ End Modified ------------------------------ */
    // memcpy(iv, input, 16);
    // memcpy(salt, input + 16, 32);
    // memcpy(R_bytes, input + 16 + 32, key_length);
    // memcpy(tag, input + 16 + 32 + key_length, 16);


    ret = cecies_hexstr2bin(private_key, key_length * 2, private_key_bytes, sizeof(private_key_bytes), &private_key_bytes_length);
    if (ret != 0 || private_key_bytes_length != key_length)
    {
        cecies_fprintf(stderr, "CECIES: Parsing decryption private key failed! Invalid hex string format or invalid key length... cecies_hexstr2bin returned %d\n", ret);
        ret = CECIES_DECRYPT_ERROR_CODE_INVALID_ARG;
        goto exit;
    }

    ret = mbedtls_mpi_read_binary(&dA, private_key_bytes, private_key_bytes_length);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: Parsing decryption private key failed! mbedtls_mpi_read_binary returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_ecp_check_privkey(&ecp_group, &dA);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: Invalid decryption private key! mbedtls_ecp_check_privkey returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_ecp_point_read_binary(&ecp_group, &R, R_bytes, key_length);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: Parsing ephemeral public key failed! mbedtls_ecp_point_read_binary returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_ecp_check_pubkey(&ecp_group, &R);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: Ephemeral public key invalid! mbedtls_ecp_check_pubkey returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_ecp_mul(&ecp_group, &S, &dA, &R, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: Ephemeral public key multiplication invalid; couldn't compute AES secret! mbedtls_ecp_mul returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_ecp_point_write_binary(&ecp_group, &S, MBEDTLS_ECP_PF_UNCOMPRESSED, &S_bytes_length, S_bytes, key_length);
    if (ret != 0 || S_bytes_length != key_length)
    {
        cecies_fprintf(stderr, "CECIES: decryption failed! Invalid ECP point; mbedtls_ecp_point_write_binary returned %d\n", ret);
        goto exit;
    }

    ret = mbedtls_md_setup(&md_ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), 1);
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: MbedTLS MD context (SHA512) setup failed! mbedtls_md_setup returned %d\n", ret);
        goto exit;
    }
    /* -------------------------------- Modified -------------------------------- */
    ret = mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA512), NULL, 0, S_bytes, S_bytes_length, NULL, 0, aes_key, 32);
    /* ------------------------------ End Modified ------------------------------ */
    if (ret != 0 || memcmp(aes_key, empty32, 32) == 0)
    {
        cecies_fprintf(stderr, "CECIES: HKDF failed! mbedtls_hkdf returned %d\n", ret);
        goto exit;
    }
    // TODO:
    /* -------------------------------- Modified -------------------------------- */
    ret = mbedtls_aes_setkey_dec(&aes_ctx, aes_key, 256);
    // ret = mbedtls_gcm_setkey(&aes_ctx, MBEDTLS_CIPHER_ID_AES, aes_key, 256);
    
    
    if (ret != 0)
    {
        cecies_fprintf(stderr, "CECIES: AES key setup failed! mbedtls_gcm_setkey returned %d\n", ret);
        goto exit;
    }

    uint8_t* decrypted = malloc(olen);
    if (decrypted == NULL)
    {
        ret = CCRUSH_ERROR_OUT_OF_MEMORY;
        goto exit;
    }

    /* -------------------------------- Modified -------------------------------- */

    // TODO:
    // AES-CBC
    mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_DECRYPT, olen, iv, input + (8 + key_length + 8), decrypted);
    
    // AES-GCM
    // ret = mbedtls_gcm_auth_decrypt( //
    //         &aes_ctx, // The MbedTLS AES context pointer.
    //         olen, // Length of the data blob to decrypt.
    //         iv, // Initialization vector which was extracted from the ciphertext.
    //         16, // Length of the IV is always 16 bytes.
    //         NULL, // No additional data.
    //         0, // ^
    //         tag, // The GCM auth tag.
    //         16, // Length of the tag.
    //         input + (16 + key_length + 16), // From where to start on reading the data to decrypt (skip the ciphertext prefix of IV, Salt, Ephemeral key and auth tag).
    //         decrypted // Where to write the decrypted data into.
    // );

    /* ------------------------------ End Modified ------------------------------ */

    // ret = mbedtls_gcm_auth_decrypt( //
    //         &aes_ctx, // The MbedTLS AES context pointer.
    //         olen, // Length of the data blob to decrypt.
    //         iv, // Initialization vector which was extracted from the ciphertext.
    //         16, // Length of the IV is always 16 bytes.
    //         NULL, // No additional data.
    //         0, // ^
    //         tag, // The GCM auth tag.
    //         16, // Length of the tag.
    //         input + (16 + 32 + key_length + 16), // From where to start on reading the data to decrypt (skip the ciphertext prefix of IV, Salt, Ephemeral key and auth tag).
    //         decrypted // Where to write the decrypted data into.
    // );

    if (ret != 0)
    {
        free(decrypted);
        cecies_fprintf(stderr, "CECIES: decryption failed! mbedtls_gcm_auth_decrypt returned %d\n", ret);
        goto exit;
    }

    if (*decrypted == 0x78)
    {
        switch (decrypted[1])
        {
            case 0x01:
            case 0x5E:
            case 0x9C:
            case 0xDA: // Zlib header detected
            {
                uint8_t* tmp = NULL;
                size_t tmplength = 0;

                ret = ccrush_decompress(decrypted, olen, 256, &tmp, &tmplength);
                if (ret != 0)
                {
                    // If decompression fails, it still means that the decryption succeeded!
                    // In this case, maybe the data just happens to start with a valid zlib header...
                    // So, uhh, silently succeed and output the decrypted data ;D
                    break;
                }

                mbedtls_platform_zeroize(decrypted, olen);
                free(decrypted);

                ret = 0;
                *output = tmp;
                *output_length = tmplength;

                goto exit;
            }
            default: {
                break;
            }
        }
    }

    ret = 0;
    *output = decrypted;
    *output_length = olen;

exit:

    mbedtls_ecp_group_free(&ecp_group);
    // TODO:
    /* -------------------------------- Modified -------------------------------- */
    mbedtls_aes_free(&aes_ctx);
    // mbedtls_gcm_free(&aes_ctx);
    mbedtls_md_free(&md_ctx);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_mpi_free(&dA);
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_point_free(&S);

    mbedtls_platform_zeroize(iv, 16);
    mbedtls_platform_zeroize(salt, 32);
    mbedtls_platform_zeroize(aes_key, 32);
    mbedtls_platform_zeroize(R_bytes, sizeof(R_bytes));
    mbedtls_platform_zeroize(S_bytes, sizeof(S_bytes));
    mbedtls_platform_zeroize(&private_key, sizeof(private_key));
    mbedtls_platform_zeroize(private_key_bytes, sizeof(private_key_bytes));

    if (encrypted_data_base64)
    {
        free(input);
    }

    return (ret);
}

int cecies_curve25519_decrypt(const uint8_t* encrypted_data, const size_t encrypted_data_length, const int encrypted_data_base64, cecies_curve25519_key private_key, uint8_t** output, size_t* output_length)
{
    return cecies_decrypt(encrypted_data, encrypted_data_length, encrypted_data_base64, private_key.hexstring, output, output_length, 0);
}

int cecies_curve448_decrypt(const uint8_t* encrypted_data, const size_t encrypted_data_length, const int encrypted_data_base64, cecies_curve448_key private_key, uint8_t** output, size_t* output_length)
{
    return cecies_decrypt(encrypted_data, encrypted_data_length, encrypted_data_base64, private_key.hexstring, output, output_length, 1);
}