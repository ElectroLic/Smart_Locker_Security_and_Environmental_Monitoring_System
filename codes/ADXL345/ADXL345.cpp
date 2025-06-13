// ADXL345.cpp
#include "ADXL345.h"

void initialize_adxl345_spi() {
    adxl345_cs = 1;
    adxl345_spi.format(8, 3);
    adxl345_spi.frequency(2000000);

    adxl345_cs = 0;
    adxl345_spi.write(0x31);
    adxl345_spi.write(0x0B);
    adxl345_cs = 1;

    adxl345_cs = 0;
    adxl345_spi.write(0x2D);
    adxl345_spi.write(0x08);
    adxl345_cs = 1;

    ThisThread::sleep_for(100ms);
    adxl345_cs = 0;
    adxl345_spi.write(0x80 | 0x00);
    char device_id = adxl345_spi.write(0x00);
    adxl345_cs = 1;

    printf("DEBUG: ADXL345 Device ID: 0x%X (Expected 0xE5)\n\r", (unsigned char)device_id);

    if (device_id == 0xE5) {
        printf("DEBUG: ADXL345 Initialisation successful!\n\r");
    } else {
        printf("ERROR: ADXL345 Initialisation FAILED!\n\r");
    }
}

void read_adxl345_data() {
    adxl345_cs = 0;
    adxl345_spi.write(0x80 | 0x40 | 0x32);

    for (int i = 0; i < 6; i++) {
        adxl_buffer[i] = adxl345_spi.write(0x00);
    }
    adxl345_cs = 1;

    adxl_raw_data[0] = (adxl_buffer[1] << 8) | adxl_buffer[0];
    adxl_raw_data[1] = (adxl_buffer[3] << 8) | adxl_buffer[2];
    adxl_raw_data[2] = (adxl_buffer[5] << 8) | adxl_buffer[4];

    accel_x = 0.004 * adxl_raw_data[0];
    accel_y = 0.004 * adxl_raw_data[1];
    accel_z = 0.004 * adxl_raw_data[2];

    printf("ADXL345: X = %+1.2f g\t Y = %+1.2f g\t Z = %+1.2f g\n\r", accel_x, accel_y, accel_z);
}
