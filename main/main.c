#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mqttapp.h"
#include "ina3221.h"

void app_main() {
    start_mesh();
    mqtt_start();
    vTaskDelay(pdMS_TO_TICKS(2000));

    ina3221_init();
    mqtt_subscribe("/test_topic", 1);

    char mqtt_msg[128];
    float bus_voltage, shunt_voltage, current;

    while (1) {
        for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++) {
            ina3221_read(&bus_voltage, &shunt_voltage, &current, i);

            snprintf(mqtt_msg, sizeof(mqtt_msg),
                     "{ \"channel\": %u, \"bus_voltage\": %.2f, \"shunt_voltage\": %.2f, \"current\": %.2f }",
                     i + 1, bus_voltage, shunt_voltage, current);

            mqtt_publish("/sensor/ina3221", mqtt_msg, 1, 0);
            ESP_LOGI("INA3221", "Published: %s", mqtt_msg);
        }

        vTaskDelay(pdMS_TO_TICKS(5000)); // Gửi mỗi 5 giây
    }
}
