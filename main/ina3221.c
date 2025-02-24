#include "ina3221.h"
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define I2C_PORT 0

ina3221_t dev;

void ina3221_init() {
    dev = (ina3221_t) {
        .shunt = {100, 100, 100}, // 100mOhm shunt
        .config.config_register = INA3221_DEFAULT_CONFIG,
        .mask.mask_register = INA3221_DEFAULT_MASK
    };

    memset(&dev.i2c_dev, 0, sizeof(i2c_dev_t));

    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(ina3221_init_desc(&dev, CONFIG_EXAMPLE_I2C_ADDR, I2C_PORT, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(ina3221_set_options(&dev, true, true, true));
    ESP_ERROR_CHECK(ina3221_enable_channel(&dev, true, true, true));
    ESP_ERROR_CHECK(ina3221_set_average(&dev, INA3221_AVG_64));
    ESP_ERROR_CHECK(ina3221_set_bus_conversion_time(&dev, INA3221_CT_2116));
    ESP_ERROR_CHECK(ina3221_set_shunt_conversion_time(&dev, INA3221_CT_2116));
}

void ina3221_read(float *bus_voltage, float *shunt_voltage, float *current, uint8_t channel) {
    ESP_ERROR_CHECK(ina3221_get_bus_voltage(&dev, channel, bus_voltage));
    ESP_ERROR_CHECK(ina3221_get_shunt_value(&dev, channel, shunt_voltage, current));
}
