#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "http_server.h"
#include "image_processing.h"

#define TAG "http_server"
#define RX_BUF_SIZE 512

static httpd_handle_t server = NULL;

// HANDLERS: root
// TODO update on changed image
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const char resp[] = 
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<title>ESP32 Image Server</title>"
        "<script>"
        "var lastSize = 0;"
        "async function checkForUpdate() {"
        "  try {"
        "    const response = await fetch('/status');"
        "    const data = await response.json();"
        "    if (data.image_size && data.image_size !== lastSize) {"
        "      lastSize = data.image_size;"
        "      document.getElementById('liveImage').src = '/image/latest.jpg?t=' + Date.now();"
        "    }"
        "  } catch(e) {}"
        "}"
        "setInterval(checkForUpdate, 2000);"
        "</script>"
        "</head>"
        "<body>"
        "<h1>ESP32 Image Server</h1>"
        "<h2>Upload Image</h2>"
        "<p>Use curl to upload: <code>curl -X POST --data-binary @image.jpg http://192.168.4.1/upload</code></p>"
        "<h2>latest/h2>"
        "<img id='liveImage' src='/image/latest.jpg' style='max-width:100%'><br>"
        "<p><a href='/image/latest.jpg'>Download latest.jpg</a></p>"
        "</body>"
        "</html>";
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    struct stat st;
    char resp[128];
    if (stat("/spiffs/latest.jpg", &st) == 0) {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"image_size\":%ld}", st.st_size);
    } else {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"image_size\":0}");
    }
    
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char buf[RX_BUF_SIZE];
    int remaining = req->content_len;
    int total = 0;
    bool first_chunk = true;

    ESP_LOGI(TAG, "Upload start: size=%d", remaining);

    // Reject oversized uploads
    #define MAX_IMAGE_SIZE (500*1024)  // 500 KB
    if (remaining > MAX_IMAGE_SIZE) {
        ESP_LOGE(TAG, "File too large: %d bytes", remaining);
        httpd_resp_send_err(req, 413, "File too large");
        return ESP_FAIL;
    }

    // Optional filename header
    char filename[64];
    if (httpd_req_get_hdr_value_str(req, "X-Filename", filename, sizeof(filename)) == ESP_OK) {
        ESP_LOGI(TAG, "Filename: %s", filename);
    }

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, remaining > RX_BUF_SIZE ? RX_BUF_SIZE : remaining);
        if (recv_len <= 0) {
            ESP_LOGE(TAG, "Receive failed");
            return ESP_FAIL;
        }

        if (!image_receive_chunk(buf, recv_len, first_chunk)) {
            ESP_LOGE(TAG, "Failed to write chunk");
            return ESP_FAIL;
        }
        first_chunk = false;

        total += recv_len;
        remaining -= recv_len;
    }

    // Finish writing the file
    image_receive_finish();

    ESP_LOGI(TAG, "Upload complete: %d bytes", total);

    httpd_resp_set_type(req, "application/json");
    char resp[256];
    snprintf(resp, sizeof(resp), "{\"result\":\"received\",\"bytes\":%d,\"url\":\"%s\"}\n", total, image_get_url());
    httpd_resp_sendstr(req, resp);

    return ESP_OK;
}

static esp_err_t image_get_handler(httpd_req_t *req)
{
    const char *filepath = "/spiffs/latest.jpg";
    
    ESP_LOGI(TAG, "Image request received for: %s", req->uri);
    
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    ESP_LOGI(TAG, "File(img) opened, size: %ld bytes", fsize);
    
    if (fsize == 0) {
        ESP_LOGE(TAG, "empty FILE");
        fclose(f);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    
    char buf[512];
    size_t read_bytes;
    size_t total_sent = 0;
    
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send chunk");
            fclose(f);
            return ESP_FAIL;
        }
        total_sent += read_bytes;
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    
    ESP_LOGI(TAG, "img scalone? (FIX): %d bytes", total_sent);
    return ESP_OK;
}

// uri ROOT
static const httpd_uri_t root_uri = {
// * HTTP route (endpoint) for the web server
// * bind {URL + HTTP + handler func} 
// match = execute
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

static const httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_get_handler
};

static const httpd_uri_t upload_uri = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler
};

static const httpd_uri_t image_uri = {
    .uri = "/image/latest.jpg",
    .method = HTTP_GET,
    .handler = image_get_handler
};

void http_server_start(void)
{
    if (server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_LOGI(TAG, "HTTP starting");

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &upload_uri);
        httpd_register_uri_handler(server, &image_uri);

        ESP_LOGI(TAG, "HTTP started");
    }
}

void http_server_stop(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
}