#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_crt_bundle.h"
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

// =========================
// Bale Bot
// =========================

#define BALE_BOT_TOKEN "644957517:x2yiPXwJG6hX6GoLshBZ9jiPVYOwSQXuOtI"
#define BALE_CHAT_ID   "644042823"

#define BALE_SEND_MESSAGE_URL "https://tapi.bale.ai/bot" BALE_BOT_TOKEN "/sendMessage"



static void start_webserver(void);
static esp_err_t send_bale_message(const char *message);

#define SDA_GPIO 27
#define SCL_GPIO 26
#define LED_GPIO GPIO_NUM_2

#define WIFI_SSID "my_len"
#define WIFI_PASSWORD "fdsavcxz7"

#define AP_SSID      "ESP32_CLOCK"
#define AP_PASSWORD  "12345678"


extern const uint8_t test_jpg_start[] asm("_binary_test_jpg_start");
extern const uint8_t test_jpg_end[]   asm("_binary_test_jpg_end");

extern const unsigned char index_html_start[] asm("_binary_index_html_start");
extern const unsigned char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "APP";
static httpd_handle_t server = NULL;
static int led_state = 0;
static volatile bool motion_led_enabled = false;
static volatile int64_t motion_led_until = 0;

i2c_master_bus_handle_t bus;
i2c_master_bus_config_t bus_config;

// =========================
// Bale getUpdates / Reset
// =========================

static int64_t bale_update_offset = 0;

static void bale_get_updates_task(void *arg)
{
    ESP_LOGI(TAG, "Bale getUpdates task started");

    char url[512];
    char response[8192];

    while (1)
    {
        snprintf(
            url,
            sizeof(url),
            "https://tapi.bale.ai/bot%s/getUpdates?timeout=20&offset=%lld",
            BALE_BOT_TOKEN,
            (long long)bale_update_offset
        );

        esp_http_client_config_t config = {
            .url = url,
            .timeout_ms = 30000,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_http_client_handle_t client =
            esp_http_client_init(&config);

        if (client == NULL)
        {
            ESP_LOGE(
                TAG,
                "Bale getUpdates: client init failed"
            );

            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_http_client_set_method(
            client,
            HTTP_METHOD_GET
        );

        esp_err_t err =
            esp_http_client_perform(client);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Bale getUpdates failed: %s",
                esp_err_to_name(err)
            );

            esp_http_client_cleanup(client);

            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int status =
            esp_http_client_get_status_code(client);

        if (status != 200)
        {
            ESP_LOGE(
                TAG,
                "Bale getUpdates HTTP status: %d",
                status
            );

            esp_http_client_cleanup(client);

            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        int len =
            esp_http_client_read_response(
                client,
                response,
                sizeof(response) - 1
            );

        esp_http_client_cleanup(client);

        if (len <= 0)
        {
            continue;
        }

        response[len] = '\0';

        ESP_LOGI(
            TAG,
            "Bale getUpdates response length: %d",
            len
        );

        cJSON *root =
            cJSON_Parse(response);

        if (root == NULL)
        {
            ESP_LOGE(
                TAG,
                "Failed to parse Bale JSON"
            );

            continue;
        }

        cJSON *ok =
            cJSON_GetObjectItem(root, "ok");

        if (!cJSON_IsTrue(ok))
        {
            ESP_LOGE(
                TAG,
                "Bale getUpdates returned ok=false"
            );

            cJSON_Delete(root);
            continue;
        }

        cJSON *result =
            cJSON_GetObjectItem(root, "result");

        if (!cJSON_IsArray(result))
        {
            cJSON_Delete(root);
            continue;
        }

        int count =
            cJSON_GetArraySize(result);

        for (int i = 0; i < count; i++)
        {
            cJSON *update =
                cJSON_GetArrayItem(result, i);

            if (update == NULL)
                continue;

            // --------------------------------
            // Update ID
            // --------------------------------

            cJSON *update_id =
                cJSON_GetObjectItem(update, "update_id");

            if (cJSON_IsNumber(update_id))
            {
                int64_t id =
                    (int64_t)update_id->valuedouble;

                if (id >= bale_update_offset)
                {
                    bale_update_offset = id + 1;
                }
            }

            // --------------------------------
            // Message
            // --------------------------------

            cJSON *message =
                cJSON_GetObjectItem(update, "message");

            if (!cJSON_IsObject(message))
                continue;

            // --------------------------------
            // Chat ID
            // --------------------------------

            cJSON *chat =
                cJSON_GetObjectItem(message, "chat");

            if (!cJSON_IsObject(chat))
                continue;

            cJSON *chat_id =
                cJSON_GetObjectItem(chat, "id");

            // --------------------------------
            // Message text
            // --------------------------------

            cJSON *text =
                cJSON_GetObjectItem(message, "text");

            if (!cJSON_IsString(text))
                continue;

            const char *received_text =
                text->valuestring;

            ESP_LOGI(
                TAG,
                "Bale message: chat_id=%s text=%s",
                cJSON_IsString(chat_id)
                    ? chat_id->valuestring
                    : "(not string)",
                received_text
            );

            // --------------------------------
            // Check our chat + reset command
            // --------------------------------

            if (
                cJSON_IsString(chat_id) &&
                strcmp(
                    chat_id->valuestring,
                    BALE_CHAT_ID
                ) == 0 &&
                strcmp(
                    received_text,
                    "reset"
                ) == 0
            )
            {
                ESP_LOGW(
                    TAG,
                    "RESET command received from Bale!"
                );

                // Optional confirmation before reboot
                send_bale_message(
                    "ESP32 reset command received"
                );

                vTaskDelay(
                    pdMS_TO_TICKS(200)
                );

                esp_restart();
            }
        }

        cJSON_Delete(root);
    }
}


static esp_err_t send_bale_message(const char *message)
{
    
    ESP_LOGI(TAG,
             "Free heap=%lu, largest block=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));


    ESP_LOGI(TAG, "Sending message to Bale...");

    char post_data[512];

    snprintf(
        post_data,
        sizeof(post_data),
        "chat_id=%s&text=%s",
        BALE_CHAT_ID,
        message
    );

    esp_http_client_config_t config = {
        .url = BALE_SEND_MESSAGE_URL,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL)
    {
        ESP_LOGE(TAG, "Bale HTTP client init failed");
        return ESP_FAIL;
    }

    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST
    );

    esp_http_client_set_header(
        client,
        "Content-Type",
        "application/x-www-form-urlencoded"
    );

    esp_http_client_set_post_field(
        client,
        post_data,
        strlen(post_data)
    );

    esp_err_t err =
        esp_http_client_perform(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(
            TAG,
            "Bale request failed: %s",
            esp_err_to_name(err)
        );

        esp_http_client_cleanup(client);
        return err;
    }

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(
        TAG,
        "Bale HTTP status = %d",
        status
    );

    char response[512];

    int len =
        esp_http_client_read_response(
            client,
            response,
            sizeof(response) - 1
        );

    if (len > 0)
    {
        response[len] = '\0';

        ESP_LOGI(
            TAG,
            "Bale response: %s",
            response
        );
    }

    esp_http_client_cleanup(client);

    if (status != 200)
    {
        ESP_LOGE(
            TAG,
            "Bale returned HTTP %d",
            status
        );

        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t send_bale_photo(const uint8_t *jpg_data, size_t jpg_len)
{
    ESP_LOGI(TAG, "Sending JPEG directly to Bale...");

    char url[256];

    snprintf(url, sizeof(url),
             "https://tapi.bale.ai/bot%s/sendPhoto",
             BALE_BOT_TOKEN);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create Bale HTTP client");
        return ESP_FAIL;
    }

    const char *boundary = "----ESP32BaleBoundary";

    char header[512];

    int header_len = snprintf(
        header,
        sizeof(header),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"photo\"; "
        "filename=\"photo.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n"
        "\r\n",
        boundary,
        BALE_CHAT_ID,
        boundary
    );

    char footer[128];

    int footer_len = snprintf(
        footer,
        sizeof(footer),
        "\r\n--%s--\r\n",
        boundary
    );

    if (header_len < 0 || footer_len < 0) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    size_t total_len =
        (size_t)header_len +
        jpg_len +
        (size_t)footer_len;

    uint8_t *body = malloc(total_len);

    if (body == NULL) {
        ESP_LOGE(TAG,
                 "Not enough memory for Bale JPEG upload: %u bytes",
                 (unsigned)total_len);

        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    memcpy(body,
           header,
           header_len);

    memcpy(body + header_len,
           jpg_data,
           jpg_len);

    memcpy(body + header_len + jpg_len,
           footer,
           footer_len);

    char content_type[128];

    snprintf(content_type,
             sizeof(content_type),
             "multipart/form-data; boundary=%s",
             boundary);

    esp_http_client_set_method(client,
                               HTTP_METHOD_POST);

    esp_http_client_set_header(client,
                               "Content-Type",
                               content_type);

    esp_http_client_set_post_field(client,
                                   (const char *)body,
                                   total_len);

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Bale photo upload failed: %s",
                 esp_err_to_name(err));

        free(body);
        esp_http_client_cleanup(client);

        return err;
    }

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(TAG,
             "Bale sendPhoto HTTP status: %d",
             status);

    free(body);

    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGE(TAG,
                 "Bale sendPhoto failed, HTTP status = %d",
                 status);

        return ESP_FAIL;
    }

    ESP_LOGI(TAG,
             "JPEG sent successfully to Bale");

    return ESP_OK;
}


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


static void photo_send_task(void *arg)
{
    ESP_LOGI(TAG, "Photo send task started");

    esp_err_t err = send_bale_photo(
        test_jpg_start,
        test_jpg_end - test_jpg_start
    );

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Photo sent successfully from background task");
    } else {
        ESP_LOGE(TAG, "Photo send failed: %s", esp_err_to_name(err));
    }

    vTaskDelete(NULL);
}



static esp_err_t photo_upload_handler(httpd_req_t *req)
{
    int total_len = req->content_len;

    if (total_len <= 0 || total_len > 500 * 1024) {
        ESP_LOGE(TAG, "Invalid JPEG size: %d", total_len);

        httpd_resp_send_err(
            req,
            HTTPD_400_BAD_REQUEST,
            "Invalid JPEG size"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "Phone JPEG upload started, size=%d bytes",
        total_len
    );

    /*
     * Bale multipart boundary
     */
    const char *boundary = "----ESP32BaleBoundary";

    char url[256];

    snprintf(
        url,
        sizeof(url),
        "https://tapi.bale.ai/bot%s/sendPhoto",
        BALE_BOT_TOKEN
    );

    /*
     * Multipart header before JPEG
     */
    char header[512];

    int header_len = snprintf(
        header,
        sizeof(header),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"chat_id\"\r\n"
        "\r\n"
        "%s\r\n"
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"photo\"; "
        "filename=\"photo.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n"
        "\r\n",
        boundary,
        BALE_CHAT_ID,
        boundary
    );

    /*
     * Multipart footer
     */
    char footer[128];

    int footer_len = snprintf(
        footer,
        sizeof(footer),
        "\r\n--%s--\r\n",
        boundary
    );

    if (header_len < 0 || footer_len < 0) {
        ESP_LOGE(TAG, "Failed to create multipart data");
        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Multipart error"
        );
        return ESP_FAIL;
    }

    /*
     * Total HTTP request sent from ESP32 to Bale.
     *
     * IMPORTANT:
     * We do NOT allocate this amount of memory.
     */
    size_t bale_content_length =
        (size_t)header_len +
        (size_t)total_len +
        (size_t)footer_len;

    ESP_LOGI(
        TAG,
        "Bale multipart content length: %u bytes",
        (unsigned)bale_content_length
    );

    /*
     * Create HTTPS client
     */
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to create Bale HTTP client");

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Bale HTTP client error"
        );

        return ESP_FAIL;
    }

    char content_type[128];

    snprintf(
        content_type,
        sizeof(content_type),
        "multipart/form-data; boundary=%s",
        boundary
    );

    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST
    );

    esp_http_client_set_header(
        client,
        "Content-Type",
        content_type
    );

    /*
     * Open Bale connection with the exact content length.
     */
    esp_err_t err =
        esp_http_client_open(
            client,
            bale_content_length
        );

    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Bale HTTP open failed: %s",
            esp_err_to_name(err)
        );

        esp_http_client_cleanup(client);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Bale connection failed"
        );

        return ESP_FAIL;
    }

    /*
     * Send multipart header
     */
    int written =
        esp_http_client_write(
            client,
            header,
            header_len
        );

    if (written != header_len) {
        ESP_LOGE(
            TAG,
            "Failed to send Bale multipart header: %d/%d",
            written,
            header_len
        );

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Bale header upload failed"
        );

        return ESP_FAIL;
    }

    /*
     * Small RAM buffer.
     *
     * The ESP32 only stores 8 KB of JPEG at a time.
     */
    uint8_t *chunk = malloc(8192);

    if (chunk == NULL) {
        ESP_LOGE(TAG, "Failed to allocate 8192-byte JPEG buffer");

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Out of memory"
        );

        return ESP_FAIL;
    }

    int received = 0;

    /*
     * Receive JPEG from Android and immediately forward
     * each chunk to Bale.
     */
    while (received < total_len) {

        int remaining = total_len - received;

        int to_receive = remaining;

        if (to_receive > 8192) {
            to_receive = 8192;
        }

        int ret =
            httpd_req_recv(
                req,
                (char *)chunk,
                to_receive
            );

        if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }

        if (ret <= 0) {
            ESP_LOGE(
                TAG,
                "JPEG receive failed: %d",
                ret
            );

            free(chunk);

            esp_http_client_close(client);
            esp_http_client_cleanup(client);

            return ESP_FAIL;
        }

        /*
         * Send this chunk to Bale.
         *
         * esp_http_client_write() normally writes the
         * requested amount, but handle partial writes too.
         */
        int chunk_sent = 0;

        while (chunk_sent < ret) {

            int n =
                esp_http_client_write(
                    client,
                    (const char *)chunk + chunk_sent,
                    ret - chunk_sent
                );

            if (n <= 0) {
                ESP_LOGE(
                    TAG,
                    "Failed sending JPEG chunk to Bale"
                );

                free(chunk);

                esp_http_client_close(client);
                esp_http_client_cleanup(client);

                httpd_resp_send_err(
                    req,
                    HTTPD_500_INTERNAL_SERVER_ERROR,
                    "Bale JPEG upload failed"
                );

                return ESP_FAIL;
            }

            chunk_sent += n;
        }

        received += ret;

        ESP_LOGD(
            TAG,
            "JPEG forwarded: %d/%d",
            received,
            total_len
        );
    }

    free(chunk);

    ESP_LOGI(
        TAG,
        "Phone JPEG completely forwarded to Bale: %d bytes",
        received
    );

    /*
     * Send multipart footer.
     */
    written =
        esp_http_client_write(
            client,
            footer,
            footer_len
        );

    if (written != footer_len) {
        ESP_LOGE(
            TAG,
            "Failed to send Bale multipart footer"
        );

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Bale footer upload failed"
        );

        return ESP_FAIL;
    }

    /*
     * Finish HTTP request and obtain Bale response.
     */
    int64_t response_length =
        esp_http_client_fetch_headers(client);

    ESP_LOGI(
        TAG,
        "Bale response content length: %lld",
        (long long)response_length
    );

    int status =
        esp_http_client_get_status_code(client);

    ESP_LOGI(
        TAG,
        "Bale sendPhoto HTTP status: %d",
        status
    );

    char response[256];

    int response_read =
        esp_http_client_read_response(
            client,
            response,
            sizeof(response) - 1
        );

    if (response_read > 0) {
        response[response_read] = '\0';

        ESP_LOGI(
            TAG,
            "Bale response: %s",
            response
        );
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {

        ESP_LOGE(
            TAG,
            "Bale sendPhoto failed, HTTP status = %d",
            status
        );

        httpd_resp_send_err(
            req,
            HTTPD_500_INTERNAL_SERVER_ERROR,
            "Bale rejected photo"
        );

        return ESP_FAIL;
    }

    ESP_LOGI(
        TAG,
        "JPEG sent successfully to Bale"
    );

    /*
     * Tell Android that the photo was successfully sent.
     */
    const char *response_ok =
        "{\"status\":\"ok\",\"message\":\"Photo sent to Bale\"}";

    httpd_resp_set_type(
        req,
        "application/json"
    );

    httpd_resp_send(
        req,
        response_ok,
        HTTPD_RESP_USE_STRLEN
    );

    return ESP_OK;
}

static esp_err_t photo_send_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Photo send button pressed");

    BaseType_t result = xTaskCreate(
        photo_send_task,
        "photo_send",
        12288,
        NULL,
        5,
        NULL
    );

    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create photo send task");

        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(
            req,
            "{\"success\":false,\"error\":\"task_create_failed\"}"
        );

        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(
        req,
        "{\"success\":true,\"message\":\"photo_send_started\"}"
    );

    return ESP_OK;
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


static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 12288;
    config.max_uri_handlers = 16;

    if (httpd_start(&server, &config) == ESP_OK) {

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler
        };

        httpd_uri_t time_uri = {
            .uri = "/api/time",
            .method = HTTP_GET,
            .handler = time_handler
        };

        httpd_uri_t led = {
            .uri = "/api/led",
            .method = HTTP_GET,
            .handler = led_handler
        };

        httpd_uri_t status = {
            .uri = "/api/status",
            .method = HTTP_GET,
            .handler = status_handler
        };

        httpd_uri_t reset = {
            .uri = "/api/reset",
            .method = HTTP_GET,
            .handler = reset_handler
        };

        httpd_uri_t photo_send_uri = {
            .uri = "/api/photo/send",
            .method = HTTP_POST,
            .handler = photo_send_handler,
            .user_ctx = NULL
        };

        httpd_uri_t photo_upload_uri = {
            .uri = "/api/photo/upload",
            .method = HTTP_POST,
            .handler = photo_upload_handler,
            .user_ctx = NULL
        };

        httpd_register_uri_handler(
            server,
            &photo_send_uri
        );

        httpd_register_uri_handler(
            server,
            &photo_upload_uri
        );

        httpd_register_uri_handler(
            server,
            &root
        );

        httpd_register_uri_handler(
            server,
            &time_uri
        );

        httpd_register_uri_handler(
            server,
            &led
        );

        httpd_register_uri_handler(
            server,
            &status
        );

        httpd_register_uri_handler(
            server,
            &reset
        );

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

        char ip[16];
        char gateway[16];

        snprintf(
            ip,
            sizeof(ip),
            IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        snprintf(
            gateway,
            sizeof(gateway),
            IPSTR,
            IP2STR(&event->ip_info.gw)
        );

        ESP_LOGI(
            TAG,
            "ESP32 IP: %s",
            ip
        );

        ESP_LOGI(
            TAG,
            "Gateway IP: %s",
            gateway
        );

        ESP_ERROR_CHECK(lcd_init(bus));

        lcd_clear();

        char line1[17];
        char line2[17];

        snprintf(
            line1,
            sizeof(line1),
            "%s",
            ip
        );

        snprintf(
            line2,
            sizeof(line2),
            "%s",
            gateway
        );

        lcd_set_cursor(0, 0);
        lcd_puts(line1);

        lcd_set_cursor(0, 1);
        lcd_puts(line2);

        /*
         * Internet available.
         * Synchronize time.
         */
        set_tehran_timezone();

        start_sntp();

        
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

    //send_test_image();

    vTaskDelay(pdMS_TO_TICKS(2000));
    send_bale_message("ESP32 is online");
    vTaskDelay(pdMS_TO_TICKS(2000));
    size_t jpg_len = test_jpg_end - test_jpg_start;
    ESP_LOGI(TAG, "Test JPEG size: %u bytes", (unsigned)jpg_len);
    send_bale_photo(test_jpg_start, jpg_len);

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
    
    xTaskCreate(
        bale_get_updates_task,
        "bale_updates",
        8192,
        NULL,
        5,
        NULL
    );
}
