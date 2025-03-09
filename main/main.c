#include <stdio.h>
#include "mesh.c"
#include "MqttCredentials.h"
#include "mqttapp.h"
#include "ina.h"
#include "driver/gpio.h"
#include "cJSON.h"

#define MOSFET_GPIO GPIO_NUM_23 // Chân GPIO điều khiển MOSFET
#define CURRENT_THRESHOLD 0.01  // Ngưỡng dòng điện để kích hoạt MOSFET (10mA)

bool other_station_charging = false; // Biến kiểm tra trạm khác có đang sạc không
bool is_charging = false; // Biến kiểm tra trạm này có đang sạc không

// Hàm callback xử lý dữ liệu nhận từ MQTT
void mqtt_data_received(const char *topic, int topic_len, const char *data, int data_len) {
    printf("Received topic: %.*s\n", topic_len, topic);
    printf("Received data: %.*s\n", data_len, data);

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        printf("Failed to parse JSON\n");
        return;
    }
    
    cJSON *charging = cJSON_GetObjectItem(json, "charging");
    if (cJSON_IsBool(charging)) {
        other_station_charging = cJSON_IsTrue(charging);
    }
    
    cJSON_Delete(json);
}

// Task gửi dữ liệu INA3221 lên MQTT
void ina3221_task(void *pvParameters)
{
    ina3221_sensor_t sensor;
    gpio_set_direction(MOSFET_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(MOSFET_GPIO, 1); // Ban đầu bật MOSFET để cho dòng qua

    if (ina3221_init_sensor(&sensor) != ESP_OK)
    {
        printf("Failed to initialize INA3221 sensor\n");
        vTaskDelete(NULL);
    }

    char payload[128]; // Chuỗi chứa dữ liệu gửi MQTT

    while (1)
    {
        float bus_voltage, shunt_voltage, shunt_current;
        ina3221_read_data(&sensor);
        bool current_detected = false;

        for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++)
        {
            // Đọc dữ liệu từ INA3221
            ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor.dev, i, &bus_voltage));
            ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor.dev, i, &shunt_voltage, &shunt_current));

            // Kiểm tra nếu có dòng điện trên kênh 1
            if (i == 0 && shunt_current > CURRENT_THRESHOLD)
            {
                current_detected = true;
            }

            // Tạo chuỗi JSON chứa dữ liệu
            snprintf(payload, sizeof(payload),
                     "{\"channel\":%u, \"bus_voltage\":%.2f, \"shunt_voltage\":%.2f, \"shunt_current\":%.2f}",
                     i + 1, bus_voltage, shunt_voltage, shunt_current);

            // Gửi lên MQTT
            mqtt_publish("/ina3221/data", payload, 1, 0);
            printf("Published: %s\n", payload);
        }

        // Nếu có xe đang sạc tại trạm này, gửi tín hiệu MQTT
        if (current_detected && !is_charging)
        {
            mqtt_publish("/charging/state", "{\"charging\":true}", 1, 0);
            is_charging = true;
        }
        else if (!current_detected && is_charging)
        {
            mqtt_publish("/charging/state", "{\"charging\":false}", 1, 0);
            is_charging = false;
        }

        // Nếu trạm này đang sạc, bật MOSFET
        // Nếu trạm khác đang sạc nhưng trạm này không sạc, tắt MOSFET
        if (is_charging || !other_station_charging) {
            gpio_set_level(MOSFET_GPIO, 1);
        } else {
            gpio_set_level(MOSFET_GPIO, 0);
        }

        printf("MOSFET state: %s\n", gpio_get_level(MOSFET_GPIO) ? "ON" : "OFF");

        vTaskDelay(pdMS_TO_TICKS(5000)); // Gửi dữ liệu mỗi 5 giây
    }
}

void app_main()
{
    gpio_set_direction(GPIO_NUM_22, GPIO_MODE_OUTPUT); // Đặt D22 làm đầu ra
    gpio_set_level(GPIO_NUM_22, 1); // Đặt mức HIGH (ON)
    
    start_mesh();
    mqtt_start();

    vTaskDelay(pdMS_TO_TICKS(2000)); // Chờ hệ thống khởi động xong

    // Đăng ký callback xử lý dữ liệu nhận từ MQTT
    mqtt_subscribe("/charging/state", 1);
//    mqtt_register_callback(mqtt_data_received);

    // Tạo task đo INA3221 và gửi dữ liệu lên MQTT
    xTaskCreate(ina3221_task, "ina3221_task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
