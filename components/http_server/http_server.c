#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "http_server.h"
#include "image_processing.h"

#define TAG "http_server"
#define RX_BUF_SIZE 512
#define MAX_IMAGE_SIZE (500 * 1024)

static httpd_handle_t server = NULL;

static const char ROOT_PAGE_HTML[] =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<title>ESP32 Image Server</title>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;line-height:1.35;padding:16px;max-width:900px;margin:auto;}"
    "img{border:1px solid #ddd;border-radius:8px}"
    "pre{background:#f5f7fb;padding:12px;border-radius:8px;overflow:auto;border:1px solid #e6e9ef}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>ESP32 Image Server</h1>"
    "<h2>Upload Image</h2>"
    "<p>Use curl to upload: <code>curl -X POST --data-binary @image.jpg http://192.168.4.1/upload</code></p>"
    "<h2>Latest Image</h2>"
    "<img src='/image/latest.jpg' style='max-width:100%' onerror=\"this.style.display='none'\"><br>"
    "<p><a href='/image/latest.jpg'>Download latest.jpg</a></p>"
    "<h2>Extracted Metadata</h2>"
    "<pre id='meta'>Loading...</pre>"
    "<script>"
    "async function loadMeta(){"
    "  try{"
    "    const r=await fetch('/image/metadata.json',{cache:'no-store'});"
    "    if(!r.ok){document.getElementById('meta').textContent='No image metadata available yet.';return;}"
    "    const j=await r.json();"
    "    document.getElementById('meta').textContent=JSON.stringify(j,null,2);"
    "  }catch(e){document.getElementById('meta').textContent='Failed to load metadata.';}"
    "}"
    "loadMeta();"
    "setInterval(loadMeta,3000);"
    "</script>"
    "</body>"
    "</html>";

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, ROOT_PAGE_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    struct stat st;
    char resp[128];

    if (stat("/spiffs/latest.jpg", &st) == 0) {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"image_size\":%ld}", st.st_size);
    } else {
        snprintf(resp, sizeof(resp), "{\"status\":\"ok\",\"image_size\":0}");
    }

    httpd_resp_set_type(req, "application/json");
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

    if (remaining > MAX_IMAGE_SIZE) {
        ESP_LOGE(TAG, "File too large: %d bytes", remaining);
        httpd_resp_send_err(req, 413, "File too large");
        return ESP_FAIL;
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

    image_receive_finish();

    char resp[192];
    snprintf(resp, sizeof(resp), "{\"result\":\"received\",\"bytes\":%d,\"url\":\"%s\"}\n", total,
             image_get_url());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t image_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/latest.jpg", "rb");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t metadata_get_handler(httpd_req_t *req)
{
    char *resp = (char *)malloc(3072);
    if (!resp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
        return ESP_FAIL;
    }

    if (!image_get_metadata_json(resp, 3072)) {
        free(resp);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "{\"error\":\"metadata_not_available\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    free(resp);
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
};

static const httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_get_handler,
};

static const httpd_uri_t upload_uri = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = upload_post_handler,
};

static const httpd_uri_t image_uri = {
    .uri = "/image/latest.jpg",
    .method = HTTP_GET,
    .handler = image_get_handler,
};

static const httpd_uri_t metadata_uri = {
    .uri = "/image/metadata.json",
    .method = HTTP_GET,
    .handler = metadata_get_handler,
};

void http_server_start(void)
{
    if (server) return;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ESP_LOGI(TAG, "HTTP starting");
    config.stack_size = 8192;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &status_uri);
        httpd_register_uri_handler(server, &upload_uri);
        httpd_register_uri_handler(server, &image_uri);

        httpd_register_uri_handler(server, &metadata_uri);
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