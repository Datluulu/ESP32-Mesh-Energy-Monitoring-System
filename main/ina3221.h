#ifndef INA3221_H
#define INA3221_H

#include <ina3221.h>

void ina3221_init();
void ina3221_read(float *bus_voltage, float *shunt_voltage, float *current, uint8_t channel);

#endif // INA3221_H
