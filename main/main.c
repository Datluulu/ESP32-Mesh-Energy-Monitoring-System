#include <stdio.h>
#include "mesh.c"
#include "MqttCredentials.h"
#include "mqttapp.h"
#include "ina.h"

// Hàm callback xử lý dữ liệu nhận
void mqtt_data_received(const char *topic, int topic_len, const char *data, int data_len) {
    printf("Received topic: %.*s\n", topic_len, topic);
    printf("Received data: %.*s\n", data_len, data);
}

// Task gửi dữ liệu INA3221 lên MQTT
void ina3221_task(void *pvParameters)
{
    ina3221_sensor_t sensor;

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

        for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++)
        {
            // Đọc dữ liệu từ INA3221
            ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor.dev, i, &bus_voltage));
            ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor.dev, i, &shunt_voltage, &shunt_current));

            // Tạo chuỗi JSON chứa dữ liệu
            snprintf(payload, sizeof(payload),
                     "{\"channel\":%u, \"bus_voltage\":%.2f, \"shunt_voltage\":%.2f, \"shunt_current\":%.2f}",
                     i + 1, bus_voltage, shunt_voltage, shunt_current);

            // Gửi lên MQTT
            mqtt_publish("/ina3221/data", payload, 1, 0);
            printf("Published: %s\n", payload);
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Gửi dữ liệu mỗi 5 giây
    }
}

void app_main()
{
    start_mesh();
    mqtt_start();

    vTaskDelay(pdMS_TO_TICKS(2000)); // Chờ hệ thống khởi động xong

    // Đăng ký callback xử lý dữ liệu nhận
    mqtt_subscribe("/test_topic", 1);

    // Tạo task đo INA3221 và gửi dữ liệu lên MQTT
    xTaskCreate(ina3221_task, "ina3221_task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}
