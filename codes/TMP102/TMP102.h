// TMP102.h
#ifndef TMP102_H
#define TMP102_H

#include "mbed.h"

extern I2C tmp102_i2c;
extern const int TMP102_ADDRESS;
extern char tmp102_temp_reg_data[2];
extern float current_temperature_c;

void configure_tmp102();
void read_tmp102_data();

#endif // TMP102_H