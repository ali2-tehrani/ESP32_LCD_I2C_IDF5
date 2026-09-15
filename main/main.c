#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "errno.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_sntp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "lcd.h"

#include "esp_system.h"
#include "esp_http_client.h"
#include <stdint.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include <errno.h>

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"



#define WINDOWS_SERVER_IP   "10.193.5.41"
#define WINDOWS_SERVER_PORT 5000
static const char *TCP_CLIENT_TAG = "TCP_CLIENT";
static int tcp_sock = -1;
static bool tcp_started = false;
static void start_webserver(void);
static esp_err_t ota_page_handler(httpd_req_t *req);
static esp_err_t ota_upload_handler(httpd_req_t *req);
static const httpd_uri_t ota_page_uri = {
    .uri = "/ota",
    .method = HTTP_GET,
    .handler = ota_page_handler,
    .user_ctx = NULL
};

static const httpd_uri_t ota_upload_uri = {
    .uri = "/api/ota",
    .method = HTTP_POST,
    .handler = ota_upload_handler,
    .user_ctx = NULL
};

#define API_URL "http://10.193.5.41:8080/api.php"

#define SDA_GPIO 27
#define SCL_GPIO 26
#define LED_GPIO GPIO_NUM_2

#define WIFI_SSID "my_len"
#define WIFI_PASSWORD "fdsavcxz7"

#define AP_SSID      "ESP32_CLOCK"
#define AP_PASSWORD  "12345678"


extern const uint8_t test_jpg_start[] asm("_binary_test_jpg_start");
extern const uint8_t test_jpg_end[]   asm("_binary_test_jpg_end");
#define IMAGE_UPLOAD_URL "http://10.193.5.41:8080/upload.php"


extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "APP";
static httpd_handle_t server = NULL;
static int led_state = 0;
static volatile bool motion_led_enabled = false;
static volatile int64_t motion_led_until = 0;

i2c_master_bus_handle_t bus;
i2c_master_bus_config_t bus_config;

typedef struct {
    char *buffer;
    size_t max_len;
    size_t len;
} http_response_t;

static void motion_led_task(void *arg)
{
    while (1)
    {
        int64_t now =
            esp_timer_get_time() / 1000;

        if (motion_led_enabled &&
            now < motion_led_until)
        {
            gpio_set_level(LED_GPIO, 1);

            vTaskDelay(
                pdMS_TO_TICKS(300)
            );

            gpio_set_level(LED_GPIO, 0);

            vTaskDelay(
                pdMS_TO_TICKS(300)
            );
        }
        else
        {
            motion_led_enabled = false;

            gpio_set_level(
                LED_GPIO,
                0
            );

            vTaskDelay(
                pdMS_TO_TICKS(100)
            );
        }
    }
}

#define MOTION_UDP_PORT 5001

static void motion_udp_task(void *arg)
{
    char rx_buffer[64];

    struct sockaddr_in server_addr;
    struct sockaddr_in source_addr;

    int sock = socket(
        AF_INET,
        SOCK_DGRAM,
        IPPROTO_IP
    );

    if (sock < 0)
    {
        ESP_LOGE(
            TAG,
            "UDP socket create failed errno=%d",
            errno
        );

        vTaskDelete(NULL);
        return;
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr)
    );

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(MOTION_UDP_PORT);

    if (bind(
            sock,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        ) < 0)
    {
        ESP_LOGE(
            TAG,
            "UDP bind failed errno=%d",
            errno
        );

        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(
        TAG,
        "Motion UDP listening on port %d",
        MOTION_UDP_PORT
    );

    while (1)
    {
        socklen_t source_addr_len =
            sizeof(source_addr);

        int len = recvfrom(
            sock,
            rx_buffer,
            sizeof(rx_buffer) - 1,
            0,
            (struct sockaddr *)&source_addr,
            &source_addr_len
        );

        if (len < 0)
        {
            ESP_LOGE(
                TAG,
                "UDP recvfrom failed errno=%d",
                errno
            );

            vTaskDelay(
                pdMS_TO_TICKS(100)
            );

            continue;
        }

        rx_buffer[len] = '\0';

        ESP_LOGI(
            TAG,
            "MOTION UDP RX: %s",
            rx_buffer
        );

        if (strcmp(rx_buffer, "MOTION") == 0)
        {
            motion_led_enabled = true;

            motion_led_until =
                (esp_timer_get_time() / 1000)
                + 10000;

            ESP_LOGI(
                TAG,
                "Motion detected -> LED for 10 seconds"
            );
        }
        else if (strcmp(rx_buffer, "NO_MOTION") == 0)
        {
            ESP_LOGI(
                TAG,
                "No motion -> keeping LED timer running"
            );
        }
        else if (strcmp(rx_buffer, "WEB_STOP") == 0)
        {
            ESP_LOGI(
                TAG,
                "Stopping web server..."
            );

            if (server != NULL)
            {
                httpd_stop(server);
                server = NULL;
            }

            ESP_LOGI(
                TAG,
                "Web server stopped"
            );
        }

        else if (strcmp(rx_buffer, "WEB_START") == 0)
        {
            ESP_LOGI(
                TAG,
                "Starting web server..."
            );

            if (server == NULL)
            {
                start_webserver();

                ESP_LOGI(
                    TAG,
                    "Web server started"
                );
            }
            else
            {
                ESP_LOGI(
                    TAG,
                    "Web server is already running"
                );
            }
        }

    }
}

static bool tcp_send_message(const char *message)
{
    if (tcp_sock < 0) {
        ESP_LOGW(
            TCP_CLIENT_TAG,
            "TCP not connected"
        );
        return false;
    }

    int len = send(
        tcp_sock,
        message,
        strlen(message),
        0
    );

    if (len < 0) {

        ESP_LOGE(
            TCP_CLIENT_TAG,
            "send failed errno=%d",
            errno
        );

        return false;
    }

    ESP_LOGI(
        TCP_CLIENT_TAG,
        "TX: %s",
        message
    );

    return true;
}
static void tcp_process_command(char *cmd)
{
    cmd[strcspn(cmd, "\r\n")] = '\0';

    ESP_LOGI(
        TCP_CLIENT_TAG,
        "RX: %s",
        cmd
    );

    char response[256];

    if (strcmp(cmd, "STATUS") == 0) {

        time_t now;
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);

        snprintf(
            response,
            sizeof(response),
            "STATUS,LED=%d,TIME=%02d:%02d:%02d,DATE=%04d-%02d-%02d\n",
            led_state,
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec,
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday
        );

        tcp_send_message(response);

        return;
    }

    if (strcmp(cmd, "TIME") == 0) {

        time_t now;
        struct tm timeinfo;

        time(&now);
        localtime_r(&now, &timeinfo);

        snprintf(
            response,
            sizeof(response),
            "TIME,%02d:%02d:%02d,%04d-%02d-%02d\n",
            timeinfo.tm_hour,
            timeinfo.tm_min,
            timeinfo.tm_sec,
            timeinfo.tm_year + 1900,
            timeinfo.tm_mon + 1,
            timeinfo.tm_mday
        );

        tcp_send_message(response);

        return;
    }

    if (strncmp(cmd, "LED,", 4) == 0) {

        int state = atoi(cmd + 4);

        if (state == 0 || state == 1) {
            led_state = state;

            if (!motion_led_enabled)
            {
                gpio_set_level(
                    LED_GPIO,
                    state
                );
            }

            snprintf(
                response,
                sizeof(response),
                "OK,LED=%d\n",
                state
            );

            tcp_send_message(response);

        } else {

            tcp_send_message(
                "ERROR,INVALID_LED\n"
            );
        }

        return;
    }

    if (strcmp(cmd, "WEB_STOP") == 0)
    {
        if (server != NULL)
        {
            httpd_stop(server);
            server = NULL;
        }

        tcp_send_message(
            "OK,WEB_STOPPED\n"
        );

        ESP_LOGI(
            TAG,
            "Web server stopped"
        );

        return;
    }

    if (strcmp(cmd, "RESET") == 0) {

        tcp_send_message(
            "OK,RESET\n"
        );

        vTaskDelay(
            pdMS_TO_TICKS(200)
        );

        esp_restart();

        return;
    }

    tcp_send_message(
        "ERROR,UNKNOWN_COMMAND\n"
    );
}
static void tcp_client_connection_task(void *pvParameters)
{
    char rx_buffer[512];
    char line_buffer[512];

    size_t line_len = 0;

    while (1) {

        int len = recv(
            tcp_sock,
            rx_buffer,
            sizeof(rx_buffer),
            0
        );

        if (len <= 0) {

            ESP_LOGW(
                TCP_CLIENT_TAG,
                "Windows connection lost"
            );

            break;
        }

        for (int i = 0; i < len; i++) {

            char c = rx_buffer[i];

            if (c == '\n') {

                line_buffer[line_len] = '\0';

                if (line_len > 0) {
                    tcp_process_command(
                        line_buffer
                    );
                }

                line_len = 0;

            } else {

                if (line_len <
                    sizeof(line_buffer) - 1) {

                    line_buffer[line_len++] = c;
                }
            }
        }
    }

    shutdown(
        tcp_sock,
        0
    );

    close(
        tcp_sock
    );

    tcp_sock = -1;

    vTaskDelete(NULL);
}
static void tcp_client_task(void *pvParameters)
{
    struct sockaddr_in server_addr;

    while (1) {

        ESP_LOGI(
            TCP_CLIENT_TAG,
            "Connecting to Windows %s:%d...",
            WINDOWS_SERVER_IP,
            WINDOWS_SERVER_PORT
        );

        tcp_sock = socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_IP
        );

        if (tcp_sock < 0) {

            ESP_LOGE(
                TCP_CLIENT_TAG,
                "Unable to create socket errno=%d",
                errno
            );

            vTaskDelay(
                pdMS_TO_TICKS(5000)
            );

            continue;
        }

        server_addr.sin_family = AF_INET;

        server_addr.sin_port =
            htons(WINDOWS_SERVER_PORT);

        server_addr.sin_addr.s_addr =
            inet_addr(WINDOWS_SERVER_IP);

        int err = connect(
            tcp_sock,
            (struct sockaddr *)&server_addr,
            sizeof(server_addr)
        );

        if (err != 0) {

            ESP_LOGE(
                TCP_CLIENT_TAG,
                "Connection failed errno=%d",
                errno
            );

            close(tcp_sock);

            tcp_sock = -1;

            vTaskDelay(
                pdMS_TO_TICKS(5000)
            );

            continue;
        }

        ESP_LOGI(
            TCP_CLIENT_TAG,
            "Connected to Windows!"
        );

        tcp_send_message(
            "ESP32_CONNECTED\n"
        );

        xTaskCreate(
            tcp_client_connection_task,
            "tcp_rx",
            4096,
            NULL,
            5,
            NULL
        );

        /*
         * این task بعد از قطع ارتباط دوباره
         * تلاش می‌کند متصل شود.
         */

        while (tcp_sock >= 0) {

            vTaskDelay(
                pdMS_TO_TICKS(1000)
            );
        }

        ESP_LOGI(
            TCP_CLIENT_TAG,
            "Retrying connection..."
        );
    }
}

static esp_err_t send_test_image(void)
{
    ESP_LOGI(TAG, "Sending image to XAMPP...");

    size_t image_size = test_jpg_end - test_jpg_start;

    ESP_LOGI(TAG, "Image size: %d bytes", (int)image_size);

    esp_http_client_config_t config = {
        .url = IMAGE_UPLOAD_URL,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_http_client_set_header(
        client,
        "Content-Type",
        "image/jpeg"
    );

    esp_err_t err = esp_http_client_set_post_field(
        client,
        (const char *)test_jpg_start,
        image_size
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "set_post_field failed: %s",
                 esp_err_to_name(err));

        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_perform(client);

    if (err != ESP_OK) {

        ESP_LOGE(TAG,
                 "Image upload failed: %s",
                 esp_err_to_name(err));

        esp_http_client_cleanup(client);
        return err;
    }

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(TAG,
             "XAMPP response status = %d",
             status);

    char response[256];

    int len = esp_http_client_read_response(
        client,
        response,
        sizeof(response) - 1
    );

    if (len > 0) {
        response[len] = '\0';

        ESP_LOGI(TAG,
                 "PHP response: %s",
                 response);
    }

    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG,
                 "PHP returned HTTP %d",
                 status);

        return ESP_FAIL;
    }

    return ESP_OK;
}


static esp_err_t photo_send_handler(
    httpd_req_t *req)
{
    ESP_LOGI(TAG, "Photo send button pressed");

    esp_err_t err = send_test_image();

    if (err == ESP_OK) {

        const char *response =
            "{\"ok\":true,\"message\":\"عکس با موفقیت ارسال شد\"}";

        httpd_resp_set_type(
            req,
            "application/json; charset=utf-8"
        );

        httpd_resp_send(
            req,
            response,
            HTTPD_RESP_USE_STRLEN
        );

    } else {

        const char *response =
            "{\"ok\":false,\"message\":\"خطا در ارسال عکس\"}";

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        httpd_resp_set_type(
            req,
            "application/json; charset=utf-8"
        );

        httpd_resp_send(
            req,
            response,
            HTTPD_RESP_USE_STRLEN
        );
    }

    return ESP_OK;
}


static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response =
        (http_response_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA &&
        response != NULL &&
        evt->data != NULL &&
        evt->data_len > 0) {

        size_t copy_len = evt->data_len;

        if (response->len + copy_len >= response->max_len) {
            copy_len =
                response->max_len - response->len - 1;
        }

        if (copy_len > 0) {
            memcpy(
                response->buffer + response->len,
                evt->data,
                copy_len
            );

            response->len += copy_len;
            response->buffer[response->len] = '\0';
        }
    }

    return ESP_OK;
}

static esp_err_t database_handler(httpd_req_t *req)
{
    char response[512] = {0};

    http_response_t http_response = {
        .buffer = response,
        .max_len = sizeof(response),
        .len = 0
    };

    esp_http_client_config_t config = {
        .url = "http://10.193.5.41:8080/api.php?get_last=1",
        .timeout_ms = 5000,
        .event_handler = http_event_handler,
        .user_data = &http_response
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "{\"ok\":false,\"error\":\"HTTP client init failed\"}"
        );
    }

    esp_err_t err =
        esp_http_client_perform(client);

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(
        "DATABASE",
        "HTTP status=%d err=%s response=%s",
        status,
        esp_err_to_name(err),
        response
    );

    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "{\"ok\":false,\"error\":\"Database server error\"}"
        );
    }

    httpd_resp_set_type(
        req,
        "application/json"
    );

    return httpd_resp_sendstr(
        req,
        response
    );
}

static void send_data_to_database(void)
{
    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    char date[11];
    char clock[9];

    strftime(date, sizeof(date),
             "%Y-%m-%d", &timeinfo);

    strftime(clock, sizeof(clock),
             "%H:%M:%S", &timeinfo);

    char url[256];

    snprintf(
        url,
        sizeof(url),
        "%s?date=%s&time=%s&led=%d",
        API_URL,
        date,
        clock,
        led_state
    );

    ESP_LOGI(
        TAG,
        "Database URL: %s",
        url
    );

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return;
    }

    esp_err_t err =
        esp_http_client_perform(client);

    if (err == ESP_OK) {

        int status =
            esp_http_client_get_status_code(client);

        ESP_LOGI(
            TAG,
            "HTTP Status = %d",
            status
        );

    } else {

        ESP_LOGE(
            TAG,
            "HTTP Error: %s",
            esp_err_to_name(err)
        );
    }

    esp_http_client_cleanup(client);
}

static void database_task(void *arg)
{
    while (1)
    {
        send_data_to_database();

        vTaskDelay(
            pdMS_TO_TICKS(2000)
        );
    }
}

static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start);
}

static esp_err_t time_handler(httpd_req_t *req)
{
    time_t now;
    struct tm timeinfo;

    time(&now);

    localtime_r(
        &now,
        &timeinfo
    );

    char date[20];
    char clock[20];

    strftime(
        date,
        sizeof(date),
        "%Y/%m/%d",
        &timeinfo
    );

    strftime(
        clock,
        sizeof(clock),
        "%H:%M:%S",
        &timeinfo
    );

    char json[100];

    snprintf(
        json,
        sizeof(json),
        "{\"date\":\"%s\",\"time\":\"%s\"}",
        date,
        clock
    );

    httpd_resp_set_type(
        req,
        "application/json"
    );

    return httpd_resp_sendstr(
        req,
        json
    );
}

static esp_err_t led_handler(httpd_req_t *req)
{
    char q[32] = {0};
    char value[8] = {0};

    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "state", value, sizeof(value)) == ESP_OK) {

        led_state = (strcmp(value, "1") == 0);
        gpio_set_level(LED_GPIO, led_state);

        ESP_ERROR_CHECK(lcd_init(bus));
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_puts("Web Control");
        lcd_set_cursor(0, 1);
        lcd_puts(led_state ? "LED: ON" : "LED: OFF");

        vTaskDelay(pdMS_TO_TICKS(3000));
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, led_state ? "{\"led\":1}" : "{\"led\":0}");
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"led\":%d}", led_state);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t reset_handler(httpd_req_t *req)
{
    httpd_resp_set_type(
        req,
        "text/plain"
    );

    httpd_resp_sendstr(
        req,
        "ESP32 is restarting..."
    );

    vTaskDelay(
        pdMS_TO_TICKS(200)
    );

    esp_restart();

    return ESP_OK;
}

static esp_err_t ota_page_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta charset='UTF-8'>"
        "<title>ESP32 OTA Update</title>"
        "</head>"
        "<body>"
        "<h2>ESP32 Firmware Update</h2>"
        "<input type='file' id='file' accept='.bin'>"
        "<br><br>"
        "<button onclick='upload()'>Upload Firmware</button>"
        "<p id='status'></p>"
        "<script>"
        "function upload(){"
        " const f=document.getElementById('file').files[0];"
        " if(!f){"
        "   document.getElementById('status').innerText='Please select a .bin file';"
        "   return;"
        " }"
        " document.getElementById('status').innerText='Uploading...';"
        " fetch('/api/ota',{"
        "   method:'POST',"
        "   headers:{'Content-Type':'application/octet-stream'},"
        "   body:f"
        " }).then(r=>r.text()).then(t=>{"
        "   document.getElementById('status').innerText=t;"
        " }).catch(e=>{"
        "   document.getElementById('status').innerText='Upload error: '+e;"
        " });"
        "}"
        "</script>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");

    return httpd_resp_send(
        req,
        html,
        HTTPD_RESP_USE_STRLEN
    );
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    

    ESP_LOGI(TAG, "OTA upload started");

    esp_ota_handle_t ota_handle = 0;

    const esp_partition_t *update_partition =
        esp_ota_get_next_update_partition(NULL);

    if (update_partition == NULL)
    {
        ESP_LOGE(TAG, "No OTA partition available");

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "No OTA partition available"
        );
    }

    ESP_LOGI(
        TAG,
        "Writing OTA firmware to partition: %s",
        update_partition->label
    );

    esp_err_t err = esp_ota_begin(
        update_partition,
        OTA_SIZE_UNKNOWN,
        &ota_handle
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_begin failed: %s",
            esp_err_to_name(err)
        );

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "OTA begin failed"
        );
    }

    char *buffer = malloc(8192);

    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "OTA buffer allocation failed");

        esp_ota_abort(ota_handle);

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "Memory allocation failed"
        );
    }

    int remaining = req->content_len;
    int total_received = 0;

    while (remaining > 0)
    {
        int recv_len = httpd_req_recv(
            req,
            buffer,
            remaining > 8192 ? 8192 : remaining
        );

        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }

        if (recv_len <= 0)
        {
            ESP_LOGE(
                TAG,
                "OTA receive failed"
            );

            free(buffer);

            esp_ota_abort(ota_handle);

            httpd_resp_set_status(
                req,
                "500 Internal Server Error"
            );

            return httpd_resp_sendstr(
                req,
                "OTA upload interrupted"
            );
        }

        err = esp_ota_write(
            ota_handle,
            buffer,
            recv_len
        );

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "esp_ota_write failed: %s",
                esp_err_to_name(err)
            );

            free(buffer);

            esp_ota_abort(ota_handle);

            httpd_resp_set_status(
                req,
                "500 Internal Server Error"
            );

            return httpd_resp_sendstr(
                req,
                "OTA write failed"
            );
        }

        remaining -= recv_len;
        total_received += recv_len;

        ESP_LOGI(
            TAG,
            "OTA received: %d bytes",
            total_received
        );
    }

    free(buffer);

    err = esp_ota_end(ota_handle);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_end failed: %s",
            esp_err_to_name(err)
        );

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "Invalid firmware image"
        );
    }

    err = esp_ota_set_boot_partition(
        update_partition
    );

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "esp_ota_set_boot_partition failed: %s",
            esp_err_to_name(err)
        );

        httpd_resp_set_status(
            req,
            "500 Internal Server Error"
        );

        return httpd_resp_sendstr(
            req,
            "Could not set boot partition"
        );
    }

    ESP_LOGI(
        TAG,
        "OTA successful: %d bytes",
        total_received
    );

    httpd_resp_set_type(
        req,
        "text/plain; charset=utf-8"
    );

    httpd_resp_sendstr(
        req,
        "OTA successful. ESP32 will restart..."
    );

    vTaskDelay(pdMS_TO_TICKS(1000));

    esp_restart();

    return ESP_OK;
}


static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 16;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root = {
            .uri = "/", .method = HTTP_GET, .handler = root_handler
        };

         httpd_uri_t time_uri = {
           .uri = "/api/time",
           .method = HTTP_GET,
           .handler = time_handler
         };

        httpd_uri_t led = {
            .uri = "/api/led", .method = HTTP_GET, .handler = led_handler
        };
        httpd_uri_t status = {
            .uri = "/api/status", .method = HTTP_GET, .handler = status_handler
        };

        httpd_uri_t reset = {
            .uri = "/api/reset",
            .method = HTTP_GET,
            .handler = reset_handler
        };

        
        httpd_uri_t db_uri = {
            .uri = "/api/db",
            .method = HTTP_GET,
            .handler = database_handler,
            .user_ctx = NULL
        };

        httpd_uri_t photo_send_uri = {
            .uri = "/api/photo/send",
            .method = HTTP_POST,
            .handler = photo_send_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(
            server,
            &photo_send_uri
        );
        
        httpd_register_uri_handler(server, &db_uri);
        
        
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(
           server,
           &time_uri
        );
        httpd_register_uri_handler(server, &led);
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &reset);

        httpd_register_uri_handler( server, &ota_page_uri );
        httpd_register_uri_handler( server, &ota_upload_uri );

        ESP_LOGI(TAG, "Web server started");
    }
}

static void start_sntp(void)
{
    sntp_setoperatingmode(
        SNTP_OPMODE_POLL
    );

    sntp_setservername(
        0,
        "pool.ntp.org"
    );

    //sntp_init();
}


static void wait_for_time(void)
{
    time_t now = 0;
    struct tm timeinfo = {0};

    int retry = 0;

    while (
        timeinfo.tm_year < (2020 - 1900)
        &&
        retry < 15
    )
    {
        vTaskDelay(
            pdMS_TO_TICKS(2000)
        );

        time(&now);

        localtime_r(
            &now,
            &timeinfo
        );

        retry++;
    }
}


static void set_tehran_timezone(void)
{
    setenv(
        "TZ",
        "IRST-3:30",
        1
    );

    tzset();
}

static void lcd_task(void *arg)
{
    char date[17];
    char clock[17];

    while (1)
    {
        time_t now;
        struct tm timeinfo;

        time(&now);

        localtime_r(
            &now,
            &timeinfo
        );

        strftime(
            date,
            sizeof(date),
            "%Y/%m/%d",
            &timeinfo
        );

        strftime(
            clock,
            sizeof(clock),
            "%H:%M:%S",
            &timeinfo
        );

        lcd_set_cursor(0, 0);

        lcd_print(
            "                "
        );

        lcd_set_cursor(0, 0);

        lcd_print(date);


        lcd_set_cursor(0, 1);

        lcd_print(
            "                "
        );

        lcd_set_cursor(0, 1);

        lcd_print(clock);


        vTaskDelay(
            pdMS_TO_TICKS(1000)
        );
    }
}

static void wifi_event(void *arg, esp_event_base_t base,
                       int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();

        ESP_ERROR_CHECK(lcd_init(bus));
        lcd_clear();
        lcd_set_cursor(0, 0);
        lcd_puts("WiFi retry...");

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void ip_event(void *arg, esp_event_base_t base,
                     int32_t id, void *data)
{
    if (
        base == IP_EVENT &&
        id == IP_EVENT_STA_GOT_IP
    )
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)data;

        char ip[17];

        snprintf(
            ip,
            sizeof(ip),
            IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        ESP_LOGI(
            TAG,
            "STA connected: %s",
            ip
        );

        ESP_ERROR_CHECK(lcd_init(bus));
        
        esp_netif_t *ap_netif =
            esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

        esp_netif_ip_info_t ap_ip_info;

        ESP_ERROR_CHECK(
            esp_netif_get_ip_info(
                ap_netif,
                &ap_ip_info
            )
        );

        char ap_ip[17];

        snprintf(
            ap_ip,
            sizeof(ap_ip),
            IPSTR,
            IP2STR(&ap_ip_info.ip)
        );

        lcd_clear();

        lcd_set_cursor(0, 0);
        lcd_puts("STA:");
        lcd_puts(ip);

        lcd_set_cursor(0, 1);
        lcd_puts("AP:");
        lcd_puts(ap_ip);

        vTaskDelay(pdMS_TO_TICKS(3000));

        // Internet available.
        // Synchronize time.
        set_tehran_timezone();

        start_sntp();

        if (!tcp_started) {

            tcp_started = true;

            xTaskCreate(
                tcp_client_task,
                "tcp_client",
                4096,
                NULL,
                5,
                NULL
            );
        }
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // STA interface
    esp_netif_create_default_wifi_sta();

    // AP interface
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&config)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &ip_event,
            NULL
        )
    );


    // =========================
    // STA configuration
    // =========================

    wifi_config_t sta_config = {0};

    strncpy(
        (char *)sta_config.sta.ssid,
        WIFI_SSID,
        sizeof(sta_config.sta.ssid) - 1
    );

    strncpy(
        (char *)sta_config.sta.password,
        WIFI_PASSWORD,
        sizeof(sta_config.sta.password) - 1
    );

    sta_config.sta.threshold.authmode =
        WIFI_AUTH_WPA2_PSK;


    // =========================
    // AP configuration
    // =========================

    wifi_config_t ap_config = {0};

    strncpy(
        (char *)ap_config.ap.ssid,
        AP_SSID,
        sizeof(ap_config.ap.ssid) - 1
    );

    strncpy(
        (char *)ap_config.ap.password,
        AP_PASSWORD,
        sizeof(ap_config.ap.password) - 1
    );

    ap_config.ap.ssid_len =
        strlen(AP_SSID);

    ap_config.ap.channel = 1;

    ap_config.ap.max_connection = 4;

    ap_config.ap.authmode =
        WIFI_AUTH_WPA2_PSK;


    // =========================
    // AP + STA
    // =========================

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_APSTA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &sta_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_AP,
            &ap_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    esp_netif_ip_info_t ap_ip_info;
    esp_netif_t *ap_netif =
        esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    ESP_ERROR_CHECK(
        esp_netif_get_ip_info(
            ap_netif,
            &ap_ip_info
        )
    );

    ESP_LOGI(
        TAG,
        "AP IP: " IPSTR,
        IP2STR(&ap_ip_info.ip)
    );

    ESP_LOGI(
        TAG,
        "WiFi AP started"
    );

    ESP_LOGI(
        TAG,
        "AP SSID: %s",
        AP_SSID
    );

    ESP_LOGI(
        TAG,
        "AP Password: %s",
        AP_PASSWORD
    );

    ESP_LOGI(
        TAG,
        "AP IP: 192.168.4.1"
    );
}

static void test_server_connection(void)
{
    esp_http_client_config_t config = {
        .url = "http://10.193.5.41:8080/",
        .timeout_ms = 5000
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE("TEST", "HTTP client init failed");
        return;
    }

    esp_err_t err =
        esp_http_client_perform(client);

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(
        "TEST",
        "status=%d err=%s",
        status,
        esp_err_to_name(err)
    );

    esp_http_client_cleanup(client);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    ESP_ERROR_CHECK(ret);

    led_state = 0;
    gpio_set_level(LED_GPIO, 0);

        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = SDA_GPIO;
        bus_config.scl_io_num = SCL_GPIO;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus));
    ESP_ERROR_CHECK(lcd_init(bus));

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_puts("Hello World!");
    lcd_set_cursor(0, 1);
    lcd_puts("ESP-IDF 5.x");

    vTaskDelay(pdMS_TO_TICKS(3000));

    lcd_clear();
    lcd_set_cursor(0, 0);
    lcd_puts("Connecting WiFi");

    vTaskDelay(pdMS_TO_TICKS(3000));

    wifi_init_sta();

    /*
    * Start Web Server immediately.
    *
    * It does NOT depend on Internet
    * or SNTP.
    */
    start_webserver();

    test_server_connection();
    vTaskDelay(pdMS_TO_TICKS(2000));
    //send_test_image();


    xTaskCreate(
        database_task,
        "database_task",
        4096,
        NULL,
        5,
        NULL
    );

    xTaskCreate(
        motion_led_task,
        "motion_led",
        2048,
        NULL,
        5,
        NULL
    );

    xTaskCreate(
        motion_udp_task,
        "motion_udp",
        4096,
        NULL,
        5,
        NULL
    );
}

