#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include <MqttCredentials.h>

#include "mqttapp.h" 

static const char *TAG_MQTT = "mqtt_example";
static esp_mqtt_client_handle_t client = NULL; 
static mqtt_receive_callback_t mqtt_callback = NULL; 

void mqtt_register_callback(mqtt_receive_callback_t callback) {
    mqtt_callback = callback;
}

int mqtt_publish(const char *topic, const char *data, int qos, int retain) {
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT client is not initialized!");
        return -1;
    }
    return esp_mqtt_client_publish(client, topic, data, 0, qos, retain);
}

int mqtt_subscribe(const char *topic, int qos) {
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT client is not initialized!");
        return -1;
    }

    int msg_id = -1;
    int retry_count = 0;
    const int max_retries = 1000; // Giới hạn số lần thử lại

    while (retry_count < max_retries) {
        msg_id = esp_mqtt_client_subscribe(client, topic, qos);
        if (msg_id >= 0) {
            ESP_LOGI(TAG_MQTT, "Subscribed to topic: %s, msg_id=%d", topic, msg_id);
            return msg_id;
        }

        ESP_LOGW(TAG_MQTT, "Failed to subscribe to topic: %s (Attempt %d/%d)", topic, retry_count + 1, max_retries);
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // Chờ 1 giây trước khi thử lại
    }

    ESP_LOGE(TAG_MQTT, "Failed to subscribe to topic: %s after %d attempts", topic, max_retries);
    return -1;
}


static void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG_MQTT, "Event dispatched: base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
        mqtt_subscribe("/topic/qos0", 0);
        mqtt_subscribe("/topic/qos1", 1);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("Reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("Reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("Captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
        }
        break;
    default:
        ESP_LOGI(TAG_MQTT, "Other event id: %d", event->event_id);
        break;
    }
}

// ==== THÊM HÀM KHỞI TẠO & KẾT NỐI MQTT ====

void mqtt_init(void) {
    if (client != NULL) {
        ESP_LOGW(TAG_MQTT, "MQTT client is already initialized!");
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_FULL_PATH,
        .credentials.username = MQTT_BROKER_USERNAME,
        .credentials.authentication.password = MQTT_BROKER_PASSWORD
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "Failed to initialize MQTT client!");
        return;
    }
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_LOGI(TAG_MQTT, "MQTT client initialized.");
}

void mqtt_connect(void) {
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT client is not initialized!");
        return;
    }
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG_MQTT, "MQTT client started.");
}

void mqtt_disconnect(void) {
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT client is not initialized!");
        return;
    }
    esp_mqtt_client_stop(client);
    ESP_LOGI(TAG_MQTT, "MQTT client stopped.");
}

void mqtt_deinit(void) {
    if (client == NULL) {
        ESP_LOGE(TAG_MQTT, "MQTT client is not initialized!");
        return;
    }
    esp_mqtt_client_destroy(client);
    client = NULL;
    ESP_LOGI(TAG_MQTT, "MQTT client deinitialized.");
}

// ==== CHẠY MQTT (GỌI ĐỂ KHỞI ĐỘNG) ====
void mqtt_start(void) {
    ESP_LOGI(TAG_MQTT, "[APP] Startup..");
    ESP_LOGI(TAG_MQTT, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG_MQTT, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);

    mqtt_init();
    mqtt_connect();
}
