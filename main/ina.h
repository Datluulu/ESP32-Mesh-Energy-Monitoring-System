#ifndef INA3221_H
#define INA3221_H

#include <inttypes.h>
#include <stdbool.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ina3221.h>
#include <string.h>
#include <esp_err.h>

// Định nghĩa các macro cấu hình
#define I2C_PORT 0
#define WARNING_CHANNEL 1
#define WARNING_CURRENT (40.0)

#if defined EXAMPLE_MEASURING_MODE_TRIGGER
#define MODE false  // true: continuous measurements, false: trigger measurements
#else
#define MODE true
#endif

// Cấu trúc lưu trữ thông tin cảm biến INA3221
typedef struct {
    ina3221_t dev;
} ina3221_sensor_t;

// Khởi tạo cảm biến INA3221
esp_err_t ina3221_init_sensor(ina3221_sensor_t *sensor);

// Đọc dữ liệu từ INA3221
void ina3221_read_data(ina3221_sensor_t *sensor);

#endif // INA3221_H
