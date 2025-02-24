
#include <mesh.c>

#include <MqttCredentials.h>

#include "mqttapp.h"






void my_mqtt_callback(const char *topic, const char *data) {
    printf("Received from topic: %s\nData: %s\n", topic, data);
}

void app_main(void) {
    mqtt_register_callback(my_mqtt_callback);
    mqtt_start();

    while(1){
        mqtt_publish("/esp/test", "Hello, MQTT!", 1, 0);
        mqtt_subscribe("/esp/test", 1);}
    
}