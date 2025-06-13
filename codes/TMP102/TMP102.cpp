// TMP102.cpp
#include "TMP102.h"

#define TMP102_TEMP_REG 0x00
#define TMP102_CONFIG_REG 0x01

void configure_tmp102() {
    char config_data[3];
    config_data[0] = TMP102_CONFIG_REG;
    config_data[1] = 0x60;
    config_data[2] = 0xA0;
    tmp102_i2c.write(TMP102_ADDRESS, config_data, 3);
}

void read_tmp102_data() {
    char temp_reg_address[1];
    temp_reg_address[0] = TMP102_TEMP_REG;
    tmp102_i2c.write(TMP102_ADDRESS, temp_reg_address, 1);

    tmp102_i2c.read(TMP102_ADDRESS, tmp102_temp_reg_data, 2);

    unsigned short raw_temp = (tmp102_temp_reg_data[0] << 4) | (tmp102_temp_reg_data[1] >> 4);
    current_temperature_c = 0.0625 * raw_temp;

    printf("TMP102: Temperature = %.3f C\n\r", current_temperature_c);
}
