    #include "mesh.h"
    #include "MqttCredentials.h"
    #include "mqttapp.h"
    #include "ina.h"

    #include "driver/gpio.h"
    #include "esp_wifi.h"
    #include "esp_netif.h"
    #include "esp_timer.h"
    #include "cJSON.h"

    #include <stdio.h>
    #include <string.h>

    #define MOSFET_GPIO_1           GPIO_NUM_23  // MOSFET 1
    #define MOSFET_GPIO_2           GPIO_NUM_22  // MOSFET 2

    #define CURRENT_THRESHOLD       50
    #define CHARGING_TIMEOUT_US     (60ULL * 100000ULL) // 60 giây

    static bool other_station_charging = false;
    static bool is_charging = false;
    static uint64_t last_other_charging_time_us = 0;

    // Lấy địa chỉ MAC của thiết bị
    void get_device_mac(char *mac_str, size_t len) {
        uint8_t mac[6];
        esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    // Lấy SSID và BSSID hiện tại
    esp_err_t get_ap_info(char *ssid, size_t ssid_len, char *bssid_str, size_t bssid_len) {
        wifi_ap_record_t ap_info;
        esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
        if (ret == ESP_OK) {
            strncpy(ssid, (char *)ap_info.ssid, ssid_len);
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

    // Chờ có IP từ WiFi
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

    // Publish MQTT
    void publish_mqtt(const char *topic, cJSON *json) {
        char *payload = cJSON_PrintUnformatted(json);
        mqtt_publish(topic, payload, 1, 0);
        printf("Published to %s: %s\n", topic, payload);
        cJSON_free(payload);
        cJSON_Delete(json);
    }

    // Hàm callback khi nhận MQTT message
    void mqtt_data_received(const char *topic, const char *data) {
        printf("RX topic: %s -> %s\n", topic, data);

        char self_mac[18];
        get_device_mac(self_mac, sizeof(self_mac));

        // ===== Xử lý điều khiển MOSFET =====
        if (strstr(topic, "/control/state")) {
            bool on = (strcasecmp(data, "true") == 0 || strcmp(data, "1") == 0);
            gpio_set_level(MOSFET_GPIO_1, on ? 1 : 0);
            gpio_set_level(MOSFET_GPIO_2, on ? 0 : 1);  // MOSFET 2 ngược lại với MOSFET 1
            printf("[MOSFET CONTROL] MOSFET 1 %s, MOSFET 2 %s (manual)\n", on ? "ON" : "OFF", on ? "OFF" : "ON");
            return; // Xử lý xong MOSFET control
        }

        // ===== Xử lý điều khiển GPIO từ topic /control/gpio/21 =====
        char topic_gpio[128];
        snprintf(topic_gpio, sizeof(topic_gpio), "/DATA/%s/control/gpio/", self_mac);

        // Kiểm tra xem topic có chứa thông tin về GPIO hay không
        if (strstr(topic, topic_gpio)) {
            // Lấy số GPIO từ topic
            int gpio_num = atoi(&topic[strlen(topic_gpio)]); // Lấy số GPIO từ phần sau "/control/gpio/"

            if (gpio_num >= 1 && gpio_num <= 39) {
                gpio_num_t gpio = (gpio_num_t)gpio_num;

                if (strstr(data, "HIGH")) {
                    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
                    gpio_set_level(gpio, 1);  // GPIO sẽ được điều khiển tại đây
                    printf("[GPIO CONTROL] GPIO_NUM_%d set HIGH\n", gpio_num);
                } else if (strstr(data, "LOW")) {
                    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
                    gpio_set_level(gpio, 0);  // GPIO sẽ được điều khiển tại đây
                    printf("[GPIO CONTROL] GPIO_NUM_%d set LOW\n", gpio_num);
                } else {
                    printf("[GPIO CONTROL] Invalid data received for GPIO_NUM_%d: %s\n", gpio_num, data);
                }
            } else {
                printf("[GPIO CONTROL] GPIO_NUM_%d is out of valid range (1-39)\n", gpio_num);
            }
            return; // Xử lý xong GPIO control cho GPIO_NUM_x
        }

        // ===== Xử lý nhận trạng thái sạc từ trạm khác =====
        if (strstr(topic, "/charging/state")) {
            cJSON *json = cJSON_Parse(data);
            if (!json) {
                printf("[CHARGING STATE] Failed to parse JSON\n");
                return;
            }

            cJSON *mac_item = cJSON_GetObjectItem(json, "self_mac");
            cJSON *charging_item = cJSON_GetObjectItem(json, "charging");

            if (cJSON_IsString(mac_item) && cJSON_IsBool(charging_item)) {
                if (strcmp(mac_item->valuestring, self_mac) != 0) {
                    if (cJSON_IsTrue(charging_item)) {
                        other_station_charging = true;
                        last_other_charging_time_us = esp_timer_get_time();
                        printf("[CHARGING STATE] Received TRUE from other station %s\n", mac_item->valuestring);
                    } else {
                        printf("[CHARGING STATE] Received FALSE from other station %s (waiting for timeout)\n", mac_item->valuestring);
                    }
                } else {
                    printf("[CHARGING STATE] Ignore self message from %s\n", self_mac);
                }
            } else {
                printf("[CHARGING STATE] Missing fields in received data\n");
            }

            cJSON_Delete(json);
            return;
        }

        // ===== Không khớp topic nào =====
        printf("[MQTT] Topic not matched any handler, ignored.\n");
    }

    // Task đọc INA3221 và publish MQTT
    void ina3221_task(void *pvParameters) {
        ina3221_sensor_t sensor;
        gpio_set_direction(MOSFET_GPIO_1, GPIO_MODE_OUTPUT);
        gpio_set_direction(MOSFET_GPIO_2, GPIO_MODE_OUTPUT);
        gpio_set_level(MOSFET_GPIO_1, 0);  // MOSFET 1 tắt ban đầu
        gpio_set_level(MOSFET_GPIO_2, 1);  // MOSFET 2 bật ban đầu

        if (ina3221_init_sensor(&sensor) != ESP_OK) {
            printf("INA3221 init failed\n");
            vTaskDelete(NULL);
        }

        char ssid[33], bssid[18], self_mac[18], topic[128];
        get_device_mac(self_mac, sizeof(self_mac));
        get_ap_info(ssid, sizeof(ssid), bssid, sizeof(bssid));

        bool prev_mosfet_state = false;

        while (true) {
            ina3221_read_data(&sensor);
            bool current_detected = false;

            for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++) {
                float bv, sv, si;
                ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor.dev, i, &bv));
                ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor.dev, i, &sv, &si));

                if (i == 0 && si > CURRENT_THRESHOLD) {
                    current_detected = true;
                }

                cJSON *j = cJSON_CreateObject();
                cJSON_AddNumberToObject(j, "bus_voltage", bv);
                cJSON_AddNumberToObject(j, "shunt_voltage", sv);
                cJSON_AddNumberToObject(j, "shunt_current", si);
                cJSON_AddNumberToObject(j, "timestamp", esp_timer_get_time());
                cJSON_AddStringToObject(j, "self_mac", self_mac);
                cJSON_AddStringToObject(j, "parent_bssid", bssid);

                snprintf(topic, sizeof(topic), "/DATA/%s/ina3221/data/%u", self_mac, i + 1);
                publish_mqtt(topic, j);
            }

            is_charging = current_detected;

            // Publish trạng thái charging
            cJSON *c = cJSON_CreateObject();
            cJSON_AddBoolToObject(c, "charging", is_charging);
            cJSON_AddNumberToObject(c, "timestamp", esp_timer_get_time());
            cJSON_AddStringToObject(c, "self_mac", self_mac);
            cJSON_AddStringToObject(c, "parent_bssid", bssid);

            snprintf(topic, sizeof(topic), "/DATA/%s/charging/state", self_mac);
            publish_mqtt(topic, c);

            uint64_t now = esp_timer_get_time();
            if (other_station_charging && (now - last_other_charging_time_us >= CHARGING_TIMEOUT_US)) {
                other_station_charging = false;
                printf("Other station charging timeout expired\n");
            }

            bool should_turn_on = (!is_charging && other_station_charging);
            gpio_set_level(MOSFET_GPIO_1, should_turn_on ? 1 : 0);  // MOSFET 1
            gpio_set_level(MOSFET_GPIO_2, should_turn_on ? 0 : 1);  // MOSFET 2 (ngược lại với MOSFET 1)

            if (prev_mosfet_state != should_turn_on) {
                prev_mosfet_state = should_turn_on;
                printf("MOSFET 1 state changed: %s, MOSFET 2 state changed: %s\n", 
                    should_turn_on ? "ON" : "OFF", 
                    should_turn_on ? "OFF" : "ON");
            }

            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    void app_main() {
        gpio_set_direction(MOSFET_GPIO_1, GPIO_MODE_OUTPUT);
        gpio_set_direction(MOSFET_GPIO_2, GPIO_MODE_OUTPUT);
        gpio_set_level(MOSFET_GPIO_1, 0);  // MOSFET 1 tắt ban đầu
        gpio_set_level(MOSFET_GPIO_2, 1);  // MOSFET 2 bật ban đầu

        start_mesh();
        mqtt_start();
        mqtt_register_callback(mqtt_data_received);
        wait_for_ip();

        char ssid[33], bssid[18], self_mac[18];
        get_device_mac(self_mac, sizeof(self_mac));
        get_ap_info(ssid, sizeof(ssid), bssid, sizeof(bssid));

        // Subscribe các topic
        char control_topic[128];
        snprintf(control_topic, sizeof(control_topic), "/DATA/%s/control/state", self_mac);
        mqtt_subscribe(control_topic, 1);

        char gpio_control_topic[128];
        snprintf(gpio_control_topic, sizeof(gpio_control_topic), "/DATA/%s/control/gpio/+", self_mac);
        mqtt_subscribe(gpio_control_topic, 1);

        char broadcast_topic[128];
        snprintf(broadcast_topic, sizeof(broadcast_topic), "/DATA/+/charging/state");
        mqtt_subscribe(broadcast_topic, 1);

        xTaskCreate(ina3221_task, "ina3221_task", 4096, NULL, 5, NULL);
    }
