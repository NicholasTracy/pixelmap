#include "pm_security.h"
#include "esp_random.h"
#include "mbedtls/sha256.h"
#include <stdio.h>
#include <string.h>

static const char *k_alphabet =
    "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

bool pm_security_ct_eq(const void *a, const void *b, size_t n)
{
    if (!a || !b) return false;
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) {
        diff |= (uint8_t)(x[i] ^ y[i]);
    }
    return diff == 0;
}

bool pm_security_is_hashed(const char *stored)
{
    if (!stored) return false;
    /* $5$ + 16 hex salt + $ + 64 hex digest */
    if (strlen(stored) < 3 + 16 + 1 + 64) return false;
    if (stored[0] != '$' || stored[1] != '5' || stored[2] != '$') return false;
    if (stored[3 + 16] != '$') return false;
    return true;
}

bool pm_security_password_ok(const char *plain)
{
    return plain && strlen(plain) >= PM_SECURITY_PASS_MIN;
}

bool pm_security_is_insecure_ap_pass(const char *pass)
{
    if (!pass || !pass[0]) return true;
    if (strcmp(pass, "pixelmap1") == 0) return true;
    if (strcmp(pass, "password") == 0) return true;
    if (strcmp(pass, "12345678") == 0) return true;
    if (strlen(pass) < 8) return true;
    return false;
}

static void to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = hex[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[in[i] & 0xf];
    }
    out[n * 2] = 0;
}

esp_err_t pm_security_hash_password(const char *plain, char *out_hash, size_t out_len)
{
    if (!plain || !out_hash || out_len < PM_SECURITY_HASH_MAX) return ESP_ERR_INVALID_ARG;

    uint8_t salt[8];
    esp_fill_random(salt, sizeof(salt));
    char salt_hex[17];
    to_hex(salt, sizeof(salt), salt_hex);

    mbedtls_sha256_context ctx;
    uint8_t dig[32];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t *)salt_hex, 16);
    mbedtls_sha256_update(&ctx, (const uint8_t *)plain, strlen(plain));
    mbedtls_sha256_finish(&ctx, dig);
    mbedtls_sha256_free(&ctx);

    char dig_hex[65];
    to_hex(dig, sizeof(dig), dig_hex);

    int n = snprintf(out_hash, out_len, "$5$%s$%s", salt_hex, dig_hex);
    if (n < 0 || (size_t)n >= out_len) return ESP_ERR_INVALID_SIZE;
    return ESP_OK;
}

bool pm_security_verify_password(const char *plain, const char *stored)
{
    if (!plain || !stored || !stored[0]) return false;

    if (!pm_security_is_hashed(stored)) {
        /* Legacy plaintext (migration). Length-aware compare. */
        size_t a = strlen(plain);
        size_t b = strlen(stored);
        if (a != b) return false;
        return pm_security_ct_eq(plain, stored, a);
    }

    char salt_hex[17];
    memcpy(salt_hex, stored + 3, 16);
    salt_hex[16] = 0;
    const char *expect = stored + 3 + 16 + 1;

    mbedtls_sha256_context ctx;
    uint8_t dig[32];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t *)salt_hex, 16);
    mbedtls_sha256_update(&ctx, (const uint8_t *)plain, strlen(plain));
    mbedtls_sha256_finish(&ctx, dig);
    mbedtls_sha256_free(&ctx);

    char dig_hex[65];
    to_hex(dig, sizeof(dig), dig_hex);
    return pm_security_ct_eq(dig_hex, expect, 64);
}

esp_err_t pm_security_random_password(char *out, size_t out_len)
{
    if (!out || out_len < PM_SECURITY_GEN_LEN + 1) return ESP_ERR_INVALID_ARG;
    size_t alen = strlen(k_alphabet);
    for (size_t i = 0; i < PM_SECURITY_GEN_LEN; ++i) {
        /* Rejection sampling for uniform alphabet index */
        uint32_t limit = (256u / (uint32_t)alen) * (uint32_t)alen;
        uint8_t b;
        do {
            esp_fill_random(&b, 1);
        } while (b >= limit);
        out[i] = k_alphabet[b % alen];
    }
    out[PM_SECURITY_GEN_LEN] = 0;
    return ESP_OK;
}
