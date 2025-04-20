#include <stdio.h>
#include "mesh.c"
#include "MqttCredentials.h"
#include "mqttapp.h"
#include "ina.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"

#define MOSFET_GPIO GPIO_NUM_23
#define CURRENT_THRESHOLD 50

bool other_station_charging = false;
bool is_charging = false;

void get_device_mac(char *mac_str, size_t max) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(mac_str, max, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void get_device_ip(char *ip_str, size_t max_len) {
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, max_len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(ip_str, max_len, "0.0.0.0");
    }
}

void publish_mqtt(const char *topic, cJSON *json_data) {
    char *payload = cJSON_PrintUnformatted(json_data);
    mqtt_publish(topic, payload, 1, 0);
    printf("Published to %s: %s\n", topic, payload);
    cJSON_free(payload);
    cJSON_Delete(json_data);
}

void mqtt_data_received_test(const char *topic, const char *data) {
    printf("Received topic: %s\n", topic);
    printf("Received data: %s\n", data);

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        printf("Failed to parse JSON\n");
        return;
    }

    cJSON *mac = cJSON_GetObjectItem(json, "mac");
    cJSON *charging = cJSON_GetObjectItem(json, "charging");

    if (mac && cJSON_IsString(mac) && charging && cJSON_IsBool(charging)) {
        char device_mac[18];
        get_device_mac(device_mac, sizeof(device_mac));

        if (strcmp(mac->valuestring, device_mac) != 0) {
            other_station_charging = cJSON_IsTrue(charging);
            printf("Other station charging: %s\n", other_station_charging ? "true" : "false");
        }
    }

    cJSON_Delete(json);
}
void mqtt_data_received(const char *topic, const char *data) {
    printf("Received topic: %s\n", topic);
    printf("Received data: %s\n", data);

    // Kiểm tra topic cụ thể (nếu bạn muốn giới hạn)
    if (strcmp(topic, "/your/custom/topic") == 0) {
        if (strcmp(data, "true") == 0) {
            gpio_set_level(MOSFET_GPIO, 1);
            printf("MOSFET ON\n");
        } else if (strcmp(data, "false") == 0) {
            gpio_set_level(MOSFET_GPIO, 0);
            printf("MOSFET OFF\n");
        } else {
            printf("Unknown command\n");
        }
        return;
    }

    // Phần xử lý JSON phía dưới vẫn giữ nguyên nếu cần
    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        printf("Failed to parse JSON\n");
        return;
    }

    cJSON *mac = cJSON_GetObjectItem(json, "mac");
    cJSON *charging = cJSON_GetObjectItem(json, "charging");

    if (mac && cJSON_IsString(mac) && charging && cJSON_IsBool(charging)) {
        char device_mac[18];
        get_device_mac(device_mac, sizeof(device_mac));

        if (strcmp(mac->valuestring, device_mac) != 0) {
            other_station_charging = cJSON_IsTrue(charging);
            printf("Other station charging: %s\n", other_station_charging ? "true" : "false");
        }
    }

    cJSON_Delete(json);
}


void ina3221_task(void *pvParameters) {
    ina3221_sensor_t sensor;
    gpio_set_direction(MOSFET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MOSFET_GPIO, 1);

    if (ina3221_init_sensor(&sensor) != ESP_OK) {
        printf("Failed to initialize INA3221 sensor\n");
        vTaskDelete(NULL);
    }

    char topic[64];
    char device_mac[18];
    char root_ip[16];
    get_device_mac(device_mac, sizeof(device_mac));
    get_device_ip(root_ip, sizeof(root_ip));


    //thu sub vao dung charging topic 
    char charging_topic[64];
    snprintf(charging_topic, sizeof(charging_topic), "/%s/%s/charging/state", root_ip, device_mac);

    mqtt_subscribe(charging_topic, 1);
    mqtt_register_callback(mqtt_data_received);
    //
    while (1) {
        float bus_voltage, shunt_voltage, shunt_current;
        ina3221_read_data(&sensor);
        bool current_detected = false;

        for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++) {
            ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor.dev, i, &bus_voltage));
            ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor.dev, i, &shunt_voltage, &shunt_current));

            if (i == 0 && shunt_current > CURRENT_THRESHOLD) {
                current_detected = true;
            }

            cJSON *json_data = cJSON_CreateObject();
            cJSON_AddNumberToObject(json_data, "bus_voltage", bus_voltage);
            cJSON_AddNumberToObject(json_data, "shunt_voltage", shunt_voltage);
            cJSON_AddNumberToObject(json_data, "shunt_current", shunt_current);
            cJSON_AddNumberToObject(json_data, "timestamp", esp_timer_get_time());

            snprintf(topic, sizeof(topic), "/%s/%s/ina3221/data/%u", root_ip, device_mac, i + 1);
            publish_mqtt(topic, json_data);
        }

        is_charging = current_detected;
        cJSON *charging_json = cJSON_CreateObject();
        cJSON_AddBoolToObject(charging_json, "charging", is_charging);
        cJSON_AddNumberToObject(charging_json, "timestamp", esp_timer_get_time());

        snprintf(topic, sizeof(topic), "/%s/%s/charging/state", root_ip, device_mac);
        publish_mqtt(topic, charging_json);

        gpio_set_level(MOSFET_GPIO, is_charging || !other_station_charging ? 1 : 0);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main() {
    start_mesh();
    mqtt_start();
    vTaskDelay(pdMS_TO_TICKS(2000));

    char device_mac[18];
    char root_ip[16];
    get_device_mac(device_mac, sizeof(device_mac));
    get_device_ip(root_ip, sizeof(root_ip));

    char control_topic[64];
    snprintf(control_topic, sizeof(control_topic), "/%s/%s/control/state", root_ip, device_mac);

    mqtt_subscribe(control_topic, 1);
    mqtt_register_callback(mqtt_data_received);
  
    xTaskCreate(ina3221_task, "ina3221_task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
