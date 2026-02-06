#include "image_processing.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

static const char *TAG = "image_proc";
static const char *stored_file = "/spiffs/latest.jpg"; 

static FILE *current_file = NULL;

void image_processing_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };
    
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initializing SPIFFS FAIL(%s)", esp_err_to_name(ret));
        ESP_LOGW(TAG, "format SPIFFS");
        esp_vfs_spiffs_unregister(NULL);
        ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS FORMAT ERR(%s)", esp_err_to_name(ret));
            return;
        }
    }
    
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS info ERR (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "total: %d bytes, used: %d bytes", total, used);
    }
    FILE *test = fopen("/spiffs/test.txt", "w");
    if (test) {
        fclose(test);
        remove("/spiffs/test.txt");
        ESP_LOGI(TAG, "SPIFFS write test OK");
    } else {
        ESP_LOGE(TAG, "SPIFFS write test FAILED");
    }
}

void image_processing_start(void)
{
    // placeholder 
}

bool image_receive_chunk(const char *buf, size_t len, bool first_chunk)
{
    if (first_chunk) {
        if (current_file) {
            fclose(current_file);
        }

        current_file = fopen(stored_file, "wb");
        if (!current_file) {
            ESP_LOGE(TAG, "Failed to open");
            return false;
        }

        // size_t total_bytes = 0;
        // uint32_t crc = 0;
        // fflush(current_file);
        // setvbuf(current_file, NULL, _IOFBF, 4096);
        // ESP_LOGI(TAG, "Starting new image transfer");
        // remove("/spiffs/previous_backup.jpg");
        // rename(stored_file, "/spiffs/previous_backup.jpg");
    }

    if (!current_file) {
        ESP_LOGE(TAG, "file not open");
        return false;
    }

    size_t written = fwrite(buf, 1, len, current_file);
    if (written != len) {
        ESP_LOGE(TAG, "len > writtern: %d/%d", written, len);
        return false;
    }

    // total_bytes += written;
    // crc = update_crc32(crc, (const uint8_t *)buf, written);
    // fflush(current_file);
    // fsync(fileno(current_file));
    // ESP_LOGD(TAG, "Chunk written: %d bytes", written);
    // if (total_bytes > MAX_FILE_SIZE) return false;
    // if (detect_end_of_image(buf, written)) fclose(current_file);

    return true;

    // fclose(current_file);
    // current_file = NULL;
    // ESP_LOGI(TAG, "Image transfer complete");
    // verify_image_integrity(stored_file);
    // send_upload_complete_event();
}


void image_receive_finish(void)
{
    if (current_file) {
        fclose(current_file);
        current_file = NULL;
        ESP_LOGI(TAG, "s %s", stored_file);
    }
}

const char* image_get_url(void)
{
    return "/image/latest.jpg";
}