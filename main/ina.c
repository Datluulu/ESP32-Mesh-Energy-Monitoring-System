#include "ina.h"
#include <stdio.h>

// Hàm khởi tạo INA3221
esp_err_t ina3221_init_sensor(ina3221_sensor_t *sensor)
{
    memset(&sensor->dev, 0, sizeof(ina3221_t));

    sensor->dev.shunt[0] = CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM;
    sensor->dev.shunt[1] = CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM;
    sensor->dev.shunt[2] = CONFIG_EXAMPLE_SHUNT_RESISTOR_MILLI_OHM;
    sensor->dev.config.config_register = INA3221_DEFAULT_CONFIG;
    sensor->dev.mask.mask_register = INA3221_DEFAULT_MASK;

    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(ina3221_init_desc(&sensor->dev, CONFIG_EXAMPLE_I2C_ADDR, I2C_PORT, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));

    // Cấu hình cảm biến
    ESP_ERROR_CHECK(ina3221_set_options(&sensor->dev, MODE, true, true));
    ESP_ERROR_CHECK(ina3221_enable_channel(&sensor->dev, true, true, true));
    ESP_ERROR_CHECK(ina3221_set_average(&sensor->dev, INA3221_AVG_64));
    ESP_ERROR_CHECK(ina3221_set_bus_conversion_time(&sensor->dev, INA3221_CT_2116));
    ESP_ERROR_CHECK(ina3221_set_shunt_conversion_time(&sensor->dev, INA3221_CT_2116));
    
    // Thiết lập cảnh báo dòng điện
    ESP_ERROR_CHECK(ina3221_set_warning_alert(&sensor->dev, WARNING_CHANNEL - 1, WARNING_CURRENT));

    return ESP_OK;
}

// Hàm đọc dữ liệu từ INA3221
void ina3221_read_data(ina3221_sensor_t *sensor)
{
    static uint32_t measure_number = 0;
    bool warning = false;
    float bus_voltage, shunt_voltage, shunt_current;

    measure_number++;

#if CONFIG_EXAMPLE_MEASURING_MODE_TRIGGER
    ESP_ERROR_CHECK(ina3221_trigger(&sensor->dev));
    printf("Trigger measurement done, waiting: ");
    do {
        printf("X");
        ESP_ERROR_CHECK(ina3221_get_status(&sensor->dev));
        if (sensor->dev.mask.wf & (1 << (3 - WARNING_CHANNEL)))
            warning = true;
        vTaskDelay(pdMS_TO_TICKS(20));
    } while (!(sensor->dev.mask.cvrf));
#else
    ESP_ERROR_CHECK(ina3221_get_status(&sensor->dev));
    if (sensor->dev.mask.wf & (1 << (3 - WARNING_CHANNEL)))
        warning = true;
#endif

    for (uint8_t i = 0; i < INA3221_BUS_NUMBER; i++)
    {
        ESP_ERROR_CHECK(ina3221_get_bus_voltage(&sensor->dev, i, &bus_voltage));
        ESP_ERROR_CHECK(ina3221_get_shunt_value(&sensor->dev, i, &shunt_voltage, &shunt_current));

        printf("\nC%u: Measure number %" PRIu32 "\n", i + 1, measure_number);
        if (warning && (i + 1) == WARNING_CHANNEL)
            printf("C%u: Warning! Current > %.2f mA !!\n", i + 1, WARNING_CURRENT);
        printf("C%u: Bus voltage: %.02f V\n", i + 1, bus_voltage);
        printf("C%u: Shunt voltage: %.02f mV\n", i + 1, shunt_voltage);
        printf("C%u: Shunt current: %.02f mA\n\n", i + 1, shunt_current);
    }
}
