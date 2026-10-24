#include "sys_info.h"
#include <inttypes.h>
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_psram.h"
#include "esp_system.h"

static const char *TAG = "sys_info";

void sys_info_print(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    size_t psram = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    const esp_partition_t *run = esp_ota_get_running_partition();

    ESP_LOGI(TAG, "==== %s %s (IDF %s) ====", app->project_name, app->version, app->idf_ver);
    ESP_LOGI(TAG, "Chip ESP32-S3 rev v%d.%d, %d core", chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
    ESP_LOGI(TAG, "PSRAM: %u KB (heap SPIRAM total %u KB)", (unsigned)(psram / 1024),
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024));
    if (run) {
        ESP_LOGI(TAG, "Running partition: %s @0x%" PRIx32 " (%" PRIu32 " KB)", run->label, run->address,
                 run->size / 1024);
    }
    ESP_LOGI(TAG, "Reset reason: %d", (int)esp_reset_reason());
    sys_info_log_heap();
}

void sys_info_log_heap(void)
{
    ESP_LOGD(TAG, "Heap: internal free %u KB (min %u KB, largest %u KB) | DMA free %u KB | PSRAM free %u KB | min ever %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(esp_get_minimum_free_heap_size() / 1024));
}
