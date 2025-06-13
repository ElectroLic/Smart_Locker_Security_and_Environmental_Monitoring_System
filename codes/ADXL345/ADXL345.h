// ADXL345.h
#ifndef ADXL345_H
#define ADXL345_H

#include "mbed.h"

extern SPI adxl345_spi;
extern DigitalOut adxl345_cs;
extern char adxl_buffer[6];
extern int16_t adxl_raw_data[3];
extern float accel_x, accel_y, accel_z;

void initialize_adxl345_spi();
void read_adxl345_data();

#endif // ADXL345_H