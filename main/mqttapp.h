#ifndef MQTTAPP_H
#define MQTTAPP_H

#include "mqtt_client.h"

// Định nghĩa kiểu callback để nhận dữ liệu từ MQTT
typedef void (*mqtt_receive_callback_t)(const char *topic, const char *data);

// Đăng ký callback để xử lý dữ liệu nhận từ MQTT
void mqtt_register_callback(mqtt_receive_callback_t callback);

// Khởi tạo MQTT (chỉ khởi tạo client, chưa kết nối)
void mqtt_init(void);

// Kết nối MQTT
void mqtt_connect(void);

// Ngắt kết nối MQTT
void mqtt_disconnect(void);

// Hủy MQTT client
void mqtt_deinit(void);

// Gửi dữ liệu lên topic MQTT
int mqtt_publish(const char *topic, const char *data, int qos, int retain);

// Đăng ký nhận dữ liệu từ một topic
int mqtt_subscribe(const char *topic, int qos);

// Hàm chính để khởi động MQTT (tích hợp init + connect)
void mqtt_start(void);

#endif // MQTTAPP_H
