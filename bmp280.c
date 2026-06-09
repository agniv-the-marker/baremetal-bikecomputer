// bmp280 i2c driver with bosch integer compensation.
// unsued since dead batch

#include "bmp280.h"
#include "i2c.h"

extern float powf(float, float); // from libm-pi.a (no math.h in tree)

// factory calibration data.
static uint16_t dig_T1, dig_P1;
static int16_t dig_T2, dig_T3, dig_P2, dig_P3, dig_P4, dig_P5,
    dig_P6, dig_P7, dig_P8, dig_P9;
static int32_t t_fine;
static unsigned dev_addr;

static int rd(uint8_t reg, uint8_t *buf, unsigned n)
{
    if (i2c_write(dev_addr, &reg, 1) < 0)
        return 0;
    return i2c_read(dev_addr, buf, n) >= 0;
}

static void wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    i2c_write(dev_addr, b, 2);
}

int bmp280_init(unsigned addr)
{
    // chip id register 0xD0 reads 0x58 for BMP280. (Cannot probe both addresses,
    // staff i2c panics on a no-ACK rather than returning an error.)
    dev_addr = addr;
    uint8_t id = 0;
    if (!rd(0xD0, &id, 1) || id != 0x58)
        return 0;

    // read 24 bytes of calibration starting at 0x88 (little-endian).
    uint8_t c[24];
    if (!rd(0x88, c, sizeof c))
        return 0;
    dig_T1 = c[0] | (c[1] << 8);
    dig_T2 = c[2] | (c[3] << 8);
    dig_T3 = c[4] | (c[5] << 8);
    dig_P1 = c[6] | (c[7] << 8);
    dig_P2 = c[8] | (c[9] << 8);
    dig_P3 = c[10] | (c[11] << 8);
    dig_P4 = c[12] | (c[13] << 8);
    dig_P5 = c[14] | (c[15] << 8);
    dig_P6 = c[16] | (c[17] << 8);
    dig_P7 = c[18] | (c[19] << 8);
    dig_P8 = c[20] | (c[21] << 8);
    dig_P9 = c[22] | (c[23] << 8);

    wr(0xF4, 0x27); // ctrl_meas: temp x1, press x1, normal mode
    wr(0xF5, 0xA0); // config: t_standby 1000ms, filter off
    delay_ms(50);
    return 1;
}

// Bosch BMP280 32-bit integer compensation (datasheet 3.11.3 / 8.2).
static int32_t compensate_T(int32_t adc_T)
{
    int32_t v1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adc_T >> 4) - (int32_t)dig_T1) *
                    ((adc_T >> 4) - (int32_t)dig_T1)) >>
                   12) *
                  (int32_t)dig_T3) >>
                 14;
    t_fine = v1 + v2;
    return (t_fine * 5 + 128) >> 8; // 0.01 deg C
}

static uint32_t compensate_P(int32_t adc_P)
{
    int32_t v1 = (((int32_t)t_fine) >> 1) - 64000;
    int32_t v2 = (((v1 >> 2) * (v1 >> 2)) >> 11) * (int32_t)dig_P6;
    v2 = v2 + ((v1 * (int32_t)dig_P5) << 1);
    v2 = (v2 >> 2) + ((int32_t)dig_P4 << 16);
    v1 = (((dig_P3 * (((v1 >> 2) * (v1 >> 2)) >> 13)) >> 3) +
          ((((int32_t)dig_P2) * v1) >> 1)) >>
         18;
    v1 = ((32768 + v1) * (int32_t)dig_P1) >> 15;
    if (v1 == 0)
        return 0; // avoid div-by-zero
    uint32_t p = (uint32_t)(((int32_t)1048576 - adc_P) - (v2 >> 12)) * 3125;
    if (p < 0x80000000u)
        p = (p << 1) / (uint32_t)v1;
    else
        p = (p / (uint32_t)v1) * 2;
    v1 = ((int32_t)dig_P9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
    v2 = ((int32_t)(p >> 2) * (int32_t)dig_P8) >> 13;
    p = (uint32_t)((int32_t)p + ((v1 + v2 + (int32_t)dig_P7) >> 4));
    return p; // Pa
}

int bmp280_read(float *temp_c, float *pressure_pa, float *altitude_m)
{
    uint8_t d[6];
    if (!rd(0xF7, d, 6)) // press(3) then temp(3), 20-bit each
        return 0;
    int32_t adc_P = ((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | (d[2] >> 4);
    int32_t adc_T = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | (d[5] >> 4);

    if (adc_T == 0 || adc_T == 0x80000 || adc_P == 0x80000)
        return 0;

    int32_t T = compensate_T(adc_T);  // sets t_fine; 0.01 C
    uint32_t P = compensate_P(adc_P); // Pa

    if (temp_c)
        *temp_c = T / 100.0f;
    if (pressure_pa)
        *pressure_pa = (float)P;
    if (altitude_m)
    // calculate altitude from pressure
    // h = 44330 * (1 - (P / P0) ^ 0.1903)
        *altitude_m =
            44330.0f * (1.0f - powf((float)P / 101325.0f, 0.1903f));
    return 1;
}
