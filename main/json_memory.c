#include "json_memory.h"
#include <stdbool.h>
#include <errno.h>
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_log.h"

static bool s_initialized, s_use_psram;

static void *json_alloc(size_t size)
{
    /* No internal fallback on PSRAM exhaustion. Only non-PSRAM hardware uses SRAM. */
    void *p = heap_caps_malloc(size, MALLOC_CAP_8BIT |
                            (s_use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL));
    if (!p) errno = ENOMEM;
    return p;
}

void fd_json_memory_init(void)
{
    if (s_initialized) return;
    s_use_psram = esp_psram_is_initialized();
    cJSON_Hooks hooks = {.malloc_fn = json_alloc, .free_fn = heap_caps_free};
    cJSON_InitHooks(&hooks);
    s_initialized = true;
    ESP_LOGI("json_mem", "cJSON allocator: %s (fixed for boot)", s_use_psram ? "PSRAM only" : "internal: PSRAM unavailable");
}
