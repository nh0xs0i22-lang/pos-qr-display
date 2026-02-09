/*
 * MQTT service – subscribes to QR display commands and parses JSON.
 *
 * Topics:
 *   pos/qr/show   → store QR payload, set has-data flag
 *   pos/qr/hide   → clear has-data flag
 *   pos/qr/result → log result (no storage yet)
 */

#include "mqtt_service.h"
#include "app_config.h"
#include "secrets/secrets.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "esp_check.h"
#include "mqtt_client.h"
#include "cJSON.h"

static const char *TAG = "mqtt";

/* ── Shared state (written by MQTT task, read by UI task) ─────────────── */

static portMUX_TYPE    s_lock   = portMUX_INITIALIZER_UNLOCKED;
static qr_payload_t    s_qr;
static volatile bool   s_has_qr;

/* ── Helpers ──────────────────────────────────────────────────────────── */

static bool topic_eq(const char *topic, int topic_len, const char *expected)
{
    int elen = (int)strlen(expected);
    return (topic_len == elen) && (memcmp(topic, expected, elen) == 0);
}

static void json_str(const cJSON *root, const char *key,
                     char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

/* ── Topic handlers ───────────────────────────────────────────────────── */

static void handle_qr_show(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "qr/show: invalid JSON");
        return;
    }

    /* Parse into a temporary so the critical section is only a memcpy. */
    qr_payload_t tmp;
    json_str(root, "qr_data", tmp.data,   sizeof(tmp.data));
    json_str(root, "amount",  tmp.amount, sizeof(tmp.amount));
    json_str(root, "desc",    tmp.desc,   sizeof(tmp.desc));
    cJSON_Delete(root);

    if (tmp.data[0] == '\0') {
        ESP_LOGW(TAG, "qr/show: missing \"qr_data\" field");
        return;
    }

    portENTER_CRITICAL(&s_lock);
    s_qr     = tmp;
    s_has_qr = true;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "QR show  qr_data=\"%.60s%s\"  amount=\"%s\"  desc=\"%s\"",
             tmp.data,
             strlen(tmp.data) > 60 ? "..." : "",
             tmp.amount,
             tmp.desc);
}

static void handle_qr_hide(void)
{
    portENTER_CRITICAL(&s_lock);
    s_has_qr = false;
    portEXIT_CRITICAL(&s_lock);

    ESP_LOGI(TAG, "QR hide");
}

static void handle_result(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "result: invalid JSON");
        return;
    }

    const cJSON *status  = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON *message = cJSON_GetObjectItemCaseSensitive(root, "message");

    ESP_LOGI(TAG, "Result  status=\"%s\"  message=\"%s\"",
             cJSON_IsString(status)  ? status->valuestring  : "(none)",
             cJSON_IsString(message) ? message->valuestring : "");

    cJSON_Delete(root);
}

/* ── MQTT event handler ───────────────────────────────────────────────── */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Connected to broker");
        esp_mqtt_client_subscribe(ev->client, APP_MQTT_TOPIC_QR_SHOW, 1);
        esp_mqtt_client_subscribe(ev->client, APP_MQTT_TOPIC_QR_HIDE, 1);
        esp_mqtt_client_subscribe(ev->client, APP_MQTT_TOPIC_RESULT,  1);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "Disconnected – will auto-reconnect");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Subscribed, msg_id=%d", ev->msg_id);
        break;

    case MQTT_EVENT_DATA:
        /* Only process complete (non-fragmented) messages. */
        if (ev->data_len != ev->total_data_len) {
            ESP_LOGW(TAG, "Fragmented message dropped (%d/%d bytes)",
                     ev->data_len, ev->total_data_len);
            break;
        }

        if (topic_eq(ev->topic, ev->topic_len, APP_MQTT_TOPIC_QR_SHOW)) {
            handle_qr_show(ev->data, ev->data_len);
        } else if (topic_eq(ev->topic, ev->topic_len, APP_MQTT_TOPIC_QR_HIDE)) {
            handle_qr_hide();
        } else if (topic_eq(ev->topic, ev->topic_len, APP_MQTT_TOPIC_RESULT)) {
            handle_result(ev->data, ev->data_len);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d",
                 ev->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* ── Public API ───────────────────────────────────────────────────────── */

esp_err_t mqtt_service_init(void)
{
    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri                = APP_MQTT_URI,
        .credentials.username              = APP_MQTT_USER,
        .credentials.authentication.password = APP_MQTT_PASS,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "client init failed");

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                       mqtt_event_handler, NULL),
        TAG, "register event handler failed");

    ESP_RETURN_ON_ERROR(
        esp_mqtt_client_start(client),
        TAG, "client start failed");

    ESP_LOGI(TAG, "Started, broker=%s", APP_MQTT_URI);
    return ESP_OK;
}

bool mqtt_service_has_qr_data(void)
{
    return s_has_qr;
}

const qr_payload_t *mqtt_service_get_qr(void)
{
    return &s_qr;
}
