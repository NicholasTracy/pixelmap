#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Minimum length for web UI / SoftAP passwords when set. */
#define PM_SECURITY_PASS_MIN 12
/** SoftAP password used only during first-boot / post-reset setup wizard. */
#define PM_SETUP_AP_PASS "pixelmap1"
/** Generated secret length (alphanumeric). */
#define PM_SECURITY_GEN_LEN 16
/** Stored web password hash: $5$<16 hex salt>$<64 hex sha256> */
#define PM_SECURITY_HASH_MAX 96

/** Constant-time equality for equal-length buffers. */
bool pm_security_ct_eq(const void *a, const void *b, size_t n);

/** True if stored value looks like a PixelMap password hash. */
bool pm_security_is_hashed(const char *stored);

/**
 * Hash plaintext into out_hash (NUL-terminated). out_len must be >= PM_SECURITY_HASH_MAX.
 */
esp_err_t pm_security_hash_password(const char *plain, char *out_hash, size_t out_len);

/**
 * Verify plaintext against stored hash or legacy plaintext.
 * Legacy plaintext is accepted only for migration (then rehash).
 */
bool pm_security_verify_password(const char *plain, const char *stored);

/** Cryptographically random password of PM_SECURITY_GEN_LEN chars + NUL. */
esp_err_t pm_security_random_password(char *out, size_t out_len);

/** True if password meets production minimum length. */
bool pm_security_password_ok(const char *plain);

/** Reject known insecure SoftAP defaults. */
bool pm_security_is_insecure_ap_pass(const char *pass);

#ifdef __cplusplus
}
#endif
