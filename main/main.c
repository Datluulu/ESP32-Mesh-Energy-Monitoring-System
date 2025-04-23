#include "mesh.h"
#include <stdio.h>
#include "MqttCredentials.h"
#include "mqttapp.h"
#include "ina.h"
#include "driver/gpio.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_timer.h"

#define MOSFET_GPIO        GPIO_NUM_23
#define CURRENT_THRESHOLD  50
#define CHARGING_TIMEOUT_US (60 * 1000000)  // 60 seconds

static bool other_station_charging = false;
static bool is_charging = false;
static uint64_t last_charging_time_us = 0;

void get_device_mac(char *mac_str, size_t max) {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(mac_str, max, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t get_ap_info(char *ssid, size_t ssid_len, char *bssid_str, size_t bssid_len) {
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        strncpy(ssid, (char*)ap_info.ssid, ssid_len);
        ssid[ssid_len - 1] = '\0';
        snprintf(bssid_str, bssid_len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                 ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
    } else {
        printf("Failed to get AP info: %s\n", esp_err_to_name(ret));
        if (ssid_len) ssid[0] = '\0';
        if (bssid_len) bssid_str[0] = '\0';
    }
    return ret;
}

void wait_for_ip() {
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    printf("Waiting for IP...\n");
    while (true) {
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK && ip_info.ip.addr != 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    printf("Got IP: " IPSTR "\n", IP2STR(&ip_info.ip));
}

void publish_mqtt(const char *topic, cJSON *json_data) {
    char *payload = cJSON_PrintUnformatted(json_data);
    mqtt_publish(topic, payload, 1, 0);
    printf("Published to %s: %s\n", topic, payload);
    cJSON_free(payload);
    cJSON_Delete(json_data);
}

void mqtt_data_received(const char *topic, const char *data) {
    printf("RX topic: %s -> %s\n", topic, data);

    // Nếu nhận lệnh điều khiển thủ công thì xử lý ngay
    if (strstr(topic, "control/state")) {
        bool on = (strcmp(data, "true") == 0);
        gpio_set_level(MOSFET_GPIO, on ? 1 : 0);
        printf("MOSFET %s (manual)\n", on ? "ON" : "OFF");
        return;
    }

    // Chỉ xử lý các bản tin trạng thái charging
    if (!strstr(topic, "/charging/state")) {
        return;
    }

    cJSON *json = cJSON_Parse(data);
    if (!json) {
        printf("Failed to parse JSON in mqtt_data_received\n");
        return;
    }

    cJSON *mac_item      = cJSON_GetObjectItem(json, "self_mac");
    cJSON *charging_item = cJSON_GetObjectItem(json, "charging");

    if (cJSON_IsString(mac_item) && cJSON_IsBool(charging_item)) {
        char self_mac[18];
        get_device_mac(self_mac, sizeof(self_mac));

        // Nếu không phải bản tin của chính node này
        if (strcmp(mac_item->valuestring, self_mac) != 0) {
            // Chỉ cập nhật flag khi nhận được TRUE
            if (cJSON_IsTrue(charging_item)) {
                other_station_charging   = true;
                last_charging_time_us    = esp_timer_get_time();
                printf("Received TRUE from other station %s → other_station_charging=TRUE\n",
                       mac_item->valuestring);
                printf("   last_charging_time_us updated to: %llu\n",
                       (unsigned long long)last_charging_time_us);
            } else {
                // Khi nhận false, chỉ log, không reset flag ngay
                printf(" Received FALSE from other station %s (will timeout in 60s)\n",
                       mac_item->valuestring);
            }
        }
    } else {
        printf(" MQTT data missing self_mac or charging fields\n");
    }

    cJSON_Delete(json);
}

void ina3221_task(void *pvParameters) {
    ina3221_sensor_t sensor;
    gpio_set_direction(MOSFET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MOSFET_GPIO, 0);  // Default is LOW

    if (ina3221_init_sensor(&sensor) != ESP_OK) {
        printf("INA3221 init failed\n");
        vTaskDelete(NULL);
    }

    char mesh_id[48], ssid[33], bssid[13], self_mac[13], topic[128];
    get_device_mac(self_mac, sizeof(self_mac));
    get_ap_info(ssid, sizeof(ssid), bssid, sizeof(bssid));
    snprintf(mesh_id, sizeof(mesh_id), "%s", ssid);

    bool prev_mosfet_state = false; // Initial state is LOW

    while (true) {
        ina3221_read_data(&sensor);
        bool current_detected = false;

        for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++) {
            float bv, sv, si;
            ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor.dev, i, &bv));
            ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor.dev, i, &sv, &si));
            if (i == 0 && si > CURRENT_THRESHOLD) current_detected = true;

            cJSON *j = cJSON_CreateObject();
            cJSON_AddNumberToObject(j, "bus_voltage", bv);
            cJSON_AddNumberToObject(j, "shunt_voltage", sv);
            cJSON_AddNumberToObject(j, "shunt_current", si);
            cJSON_AddNumberToObject(j, "timestamp", esp_timer_get_time());
            cJSON_AddStringToObject(j, "self_mac", self_mac);
            cJSON_AddStringToObject(j, "parent_bssid", bssid);
            snprintf(topic, sizeof(topic), "/%s/%s/ina3221/data/%u", mesh_id, self_mac, i + 1);
            publish_mqtt(topic, j);
        }

        is_charging = current_detected;

        // Handle charging state and MOSFET control
        cJSON *c = cJSON_CreateObject();
        cJSON_AddBoolToObject(c, "charging", is_charging);
        cJSON_AddNumberToObject(c, "timestamp", esp_timer_get_time());
        cJSON_AddStringToObject(c, "self_mac", self_mac);
        cJSON_AddStringToObject(c, "parent_bssid", bssid);
        snprintf(topic, sizeof(topic), "/%s/%s/charging/state", mesh_id, self_mac);
        publish_mqtt(topic, c);

        // Log trạng thái trước khi kiểm tra
        printf("Checking MOSFET state: Other station charging: %s, Current station charging: %s\n",
               other_station_charging ? "true" : "false", is_charging ? "true" : "false");

        // Check if any station is charging and update MOSFET state
        uint64_t current_time_us = esp_timer_get_time();
        if (other_station_charging || (is_charging && current_time_us - last_charging_time_us < CHARGING_TIMEOUT_US)) {
            // If another station is charging or current station is charging within timeout
            gpio_set_level(MOSFET_GPIO, 1);  // Set MOSFET to HIGH
            printf("MOSFET turned ON due to charging condition\n");
        } else {
            gpio_set_level(MOSFET_GPIO, 0);  // Set MOSFET to LOW
            printf("MOSFET turned OFF due to no charging condition\n");
        }

        // Update log if MOSFET state changes
        if (prev_mosfet_state != (other_station_charging || is_charging)) {
            prev_mosfet_state = (other_station_charging || is_charging);
            printf("MOSFET state changed: %s\n", prev_mosfet_state ? "ON" : "OFF");
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main() {
    gpio_set_direction(MOSFET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MOSFET_GPIO, 0);  // Default is LOW

    start_mesh();
    mqtt_start();
    mqtt_register_callback(mqtt_data_received);
    wait_for_ip();

    char mesh_id[48], ssid[33], bssid[13], self_mac[13];
    get_device_mac(self_mac, sizeof(self_mac));
    get_ap_info(ssid, sizeof(ssid), bssid, sizeof(bssid));
    snprintf(mesh_id, sizeof(mesh_id), "%s", ssid);

    char control_topic[128];
    snprintf(control_topic, sizeof(control_topic), "/%s/%s/control/state", mesh_id, self_mac);
    mqtt_subscribe(control_topic, 1);

    char broadcast_topic[128];
    snprintf(broadcast_topic, sizeof(broadcast_topic), "/%s/+/charging/state", mesh_id);
    mqtt_subscribe(broadcast_topic, 1);

    xTaskCreate(ina3221_task, "ina3221_task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
