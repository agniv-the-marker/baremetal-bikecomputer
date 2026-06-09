#ifndef __BMP280_H__
#define __BMP280_H__

// bmp280 barometric pressure + temperature sensor. retired: dead sensor batch (tests/4)
// call i2c_init before initing this. careful: staff i2c panics on NAK instead of erroring

#include "rpi.h"

// init device at i2c address <addr> (0x76 with SDO->GND, 0x77 with SDO->3V3),
// read the factory calibration and start cts measurement, 1 on success, 0 if chip id wrong
int bmp280_init(unsigned addr);

// read compensated values: temperature (deg C), pressure (Pa), and altitude
// returns 1 on success, 0 on failure
int bmp280_read(float *temp_c, float *pressure_pa, float *altitude_m);

#endif
