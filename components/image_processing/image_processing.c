#include "image_processing.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static const char *TAG = "image_proc";
static const char *stored_file = "/spiffs/latest.jpg";

static FILE *current_file = NULL;

typedef struct {
    bool available;
    bool valid_jpeg;
    size_t file_size;
    time_t mtime;
    uint16_t width;
    uint16_t height;
    uint8_t bits_per_sample;
    bool progressive;
} image_basic_metadata_t;

static image_basic_metadata_t s_meta;

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)((((uint16_t)p[0]) << 8) | p[1]);
}

static bool parse_basic_jpeg_metadata(const char *path, image_basic_metadata_t *meta)
{
    memset(meta, 0, sizeof(*meta));

    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }

    meta->available = true;
    meta->file_size = (size_t)st.st_size;
    meta->mtime = st.st_mtime;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return true;
    }

    uint8_t soi[2];
    if (fread(soi, 1, sizeof(soi), f) != sizeof(soi) || soi[0] != 0xFF || soi[1] != 0xD8) {
        fclose(f);
        return true;
    }

    meta->valid_jpeg = true;

    while (1) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        if (c != 0xFF) {
            continue;
        }

        int marker = fgetc(f);
        if (marker == EOF) {
            break;
        }
        while (marker == 0xFF) {
            marker = fgetc(f);
            if (marker == EOF) {
                break;
            }
        }
        if (marker == EOF) {
            break;
        }

        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }

        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            continue;
        }

        uint8_t len_bytes[2];
        if (fread(len_bytes, 1, 2, f) != 2) {
            break;
        }

        uint16_t seg_len = read_be16(len_bytes);
        if (seg_len < 2) {
            break;
        }
        size_t payload_len = seg_len - 2;

        if ((marker >= 0xC0 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (payload_len >= 6) {
                uint8_t sof[6];
                if (fread(sof, 1, sizeof(sof), f) == sizeof(sof)) {
                    meta->bits_per_sample = sof[0];
                    meta->height = read_be16(sof + 1);
                    meta->width = read_be16(sof + 3);
                    meta->progressive = (marker == 0xC2 || marker == 0xCA);
                }
            }
            break;
        }

        fseek(f, (long)payload_len, SEEK_CUR);
    }

    fclose(f);
    return true;
}

static void refresh_metadata(void)
{
    image_basic_metadata_t parsed;
    if (parse_basic_jpeg_metadata(stored_file, &parsed)) {
        s_meta = parsed;
    } else {
        memset(&s_meta, 0, sizeof(s_meta));
    }
}

void image_processing_init(void)
{
    ESP_LOGI(TAG, "Initializing SPIFFS");
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    if (esp_vfs_spiffs_register(&conf) != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS init failed");
        return;
    }

    refresh_metadata();
}

void image_processing_start(void)
{
    /* layout/template: to be implemented */
}

bool image_receive_chunk(const char *buf, size_t len, bool first_chunk)
{
    if (first_chunk) {
        if (current_file) {
            fclose(current_file);
        }
        current_file = fopen(stored_file, "wb");
        if (!current_file) {
            return false;
        }
    }

    if (!current_file) {
        return false;
    }

    return fwrite(buf, 1, len, current_file) == len;
}

void image_receive_finish(void)
{
    if (current_file) {
        fclose(current_file);
        current_file = NULL;
    }
    refresh_metadata();
}

const char *image_get_url(void)
{
    return "/image/latest.jpg";
}

bool image_get_metadata_json(char *out, size_t out_len)
{
    if (!out || out_len == 0 || !s_meta.available) {
        return false;
    }

    char mtime_iso[32] = "";
    if (s_meta.mtime > 0) {
        struct tm tm_data;
        if (localtime_r(&s_meta.mtime, &tm_data) != NULL) {
            strftime(mtime_iso, sizeof(mtime_iso), "%Y-%m-%dT%H:%M:%S", &tm_data);
        }
    }

    int n = snprintf(
        out, out_len,
        "{"
        "\"image\":{"
        "\"path\":\"%s\","
        "\"file_size_bytes\":%u,"
        "\"mtime_epoch\":%lld,"
        "\"mtime_local\":\"%s\","
        "\"valid_jpeg\":%s,"
        "\"width\":%u,"
        "\"height\":%u,"
        "\"bits_per_sample\":%u,"
        "\"progressive\":%s"
        "},"
        "\"capture\":{"
        "\"source\":\"to_be_implemented\","
        "\"iso\":0,"
        "\"exposure_time\":\"\","
        "\"f_number\":\"\","
        "\"focal_length\":\"\","
        "\"note\":\"to be filled by camera module or AI pipeline\""
        "},"
        "\"project\":{"
        "\"layout_template\":\"to_be_implemented\""
        "}"
        "}",
        stored_file,
        (unsigned)s_meta.file_size,
        (long long)s_meta.mtime,
        mtime_iso,
        s_meta.valid_jpeg ? "true" : "false",
        (unsigned)s_meta.width,
        (unsigned)s_meta.height,
        (unsigned)s_meta.bits_per_sample,
        s_meta.progressive ? "true" : "false");

    return n > 0 && (size_t)n < out_len;
}
