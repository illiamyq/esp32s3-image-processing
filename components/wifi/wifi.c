#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "wifi.h"

static const char *TAG = "wifi_ap_http";

esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char resp[] = "<h1>wifi connection overwrite w http</h1>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_uri_t uri_get = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
};

void webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &uri_get);
    }
}

void wifi_ap_start(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "safe island",
            .ssid_len = strlen("safe island"),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .password = "pass4444",
            .max_connection = 4,
        },
    };

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    esp_wifi_start();
}
