/* Ma PIN 4 so (xem fd_pin.h). Bam: h0 = SHA256(salt || 4 so), h(i+1) = SHA256(h(i) || salt), 2000 vong (PSA / mbedTLS 4). */
#include "fd_pin.h"
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "psa/crypto.h"

#define NS       "fd"
#define ITER     2000
#define FAIL_MAX 5
#define LOCK_S   30

static int64_t s_lock_until_us;   /* chi trong RAM: khoi dong lai = mo khoa */
static int s_fail;                /* so lan sai lien tiep (RAM) */

static bool hash_pin(const uint8_t salt[16], const char d[4], uint8_t out[32])
{
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    uint8_t buf[48];
    size_t n;
    memcpy(buf, salt, 16);
    memcpy(buf + 16, d, 4);
    if (psa_hash_compute(PSA_ALG_SHA_256, buf, 20, out, 32, &n) != PSA_SUCCESS) return false;
    for (int i = 0; i < ITER; i++) {
        memcpy(buf, out, 32);
        memcpy(buf + 32, salt, 16);
        if (psa_hash_compute(PSA_ALG_SHA_256, buf, 48, out, 32, &n) != PSA_SUCCESS) return false;
    }
    memset(buf, 0, sizeof(buf));
    return true;
}

int fd_pin_lock_left_s(void)
{
    if (!s_lock_until_us) return 0;
    int64_t now = esp_timer_get_time();
    if (now >= s_lock_until_us) {   /* het khoa: cho them 5 lan */
        s_lock_until_us = 0;
        s_fail = 0;
        return 0;
    }
    return (int)((s_lock_until_us - now + 999999) / 1000000);
}
bool fd_pin_is_set(void)
{
    nvs_handle_t h;
    size_t n = 0;
    bool ok = false;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "pin_hash", NULL, &n) == ESP_OK && n == 32;
        nvs_close(h);
    }
    return ok;
}

bool fd_pin_set(const char d[4])
{
    uint8_t salt[16], hv[32];
    esp_fill_random(salt, sizeof(salt));
    if (!hash_pin(salt, d, hv)) return false;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, "pin_salt", salt, 16) == ESP_OK && nvs_set_blob(h, "pin_hash", hv, 32) == ESP_OK &&
              nvs_commit(h) == ESP_OK;
    nvs_close(h);
    memset(hv, 0, sizeof(hv));
    s_lock_until_us = 0;
    s_fail = 0;
    return ok;
}

bool fd_pin_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    nvs_erase_key(h, "pin_salt");
    nvs_erase_key(h, "pin_hash");
    nvs_erase_key(h, "pin_req");
    bool ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    s_lock_until_us = 0;
    s_fail = 0;
    return ok;
}

bool fd_pin_verify(const char d[4])
{
    if (fd_pin_lock_left_s() > 0) return false;
    uint8_t salt[16], ref[32], hv[32];
    size_t ns = 16, nh = 32;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    bool got = nvs_get_blob(h, "pin_salt", salt, &ns) == ESP_OK && nvs_get_blob(h, "pin_hash", ref, &nh) == ESP_OK &&
               ns == 16 && nh == 32;
    nvs_close(h);
    if (!got || !hash_pin(salt, d, hv)) return false;
    uint8_t diff = 0;   /* so sanh hang thoi gian */
    for (int i = 0; i < 32; i++) diff |= hv[i] ^ ref[i];
    memset(hv, 0, sizeof(hv));
    if (diff == 0) {
        s_fail = 0;
        return true;
    }
    if (++s_fail >= FAIL_MAX) s_lock_until_us = esp_timer_get_time() + (int64_t)LOCK_S * 1000000;
    return false;
}
bool fd_pin_req_get(void)
{
    nvs_handle_t h;
    uint8_t v = 0;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, "pin_req", &v);
        nvs_close(h);
    }
    return v != 0;
}

void fd_pin_req_set(bool on)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (on) nvs_set_u8(h, "pin_req", 1);
    else nvs_erase_key(h, "pin_req");
    nvs_commit(h);
    nvs_close(h);
}
int fd_pin_fails_left(void) { return s_fail >= FAIL_MAX ? 0 : FAIL_MAX - s_fail; }
