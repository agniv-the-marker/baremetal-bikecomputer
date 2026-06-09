#include "rpi.h"
#include "ssd1306-display-driver.h"
#include "i2c.h"

static uint8_t i2c_buffer[SSD1306_I2C_BUFFER_SIZE];
static uint8_t *display_buffer = i2c_buffer + 1;

// Helper function to send a byte over I2C
void ssd1306_display_send_command(uint8_t cmd) {
  uint8_t cmd_buf[2] = {0x00, cmd};
  i2c_write(SSD1306_DISPLAY_ADDRESS, cmd_buf, 2);
}

// power: sleep the panel (display off + charge pump off) and wake it
void ssd1306_display_off(void) {
  ssd1306_display_send_command(0x8D);   // charge pump
  ssd1306_display_send_command(0x10);   // ... disable
  ssd1306_display_send_command(0xAE);   // display off
}
void ssd1306_display_on(void) {
  ssd1306_display_send_command(0x8D);   // charge pump
  ssd1306_display_send_command(0x14);   // ... enable
  ssd1306_display_send_command(0xAF);   // display on
}

// Initialize the display.
// Requirement: I2C should have been initialized beforehand.
void ssd1306_display_init(void) {
  /* Display initialization flow [SSD1306 datasheet pg 64] */

  // 0. Turn the display off to be safe [SSD1306 pg 28]
  ssd1306_display_send_command(0xAE);

  // 1. Set multiplex ratio = 64 (display has 64 rows)
  ssd1306_display_send_command(0xA8);
  ssd1306_display_send_command(0x3F);

  // 2. Set display offset = 0 [SSD1306 pg 37]
  ssd1306_display_send_command(0xD3);
  ssd1306_display_send_command(0x00);

  // 3. Set display start line = 0 (single-byte cmd 0x40 | line) [SSD1306 pg 36]
  ssd1306_display_send_command(0x40);

  // 4. Segment re-map: column 127 -> SEG0 (horizontal flip) [SSD1306 pg 36]
  ssd1306_display_send_command(0xA1);

  // 5. COM output scan direction = remapped (vertical flip)
  ssd1306_display_send_command(0xC8);

  // 6. COM pins: alternate config, no L/R remap [SSD1306 pg 40]
  ssd1306_display_send_command(0xDA);
  ssd1306_display_send_command(0x12);

  // 7. Contrast = 0xCF (mid-bright) [SSD1306 pg 36]
  ssd1306_display_send_command(0x81);
  ssd1306_display_send_command(0xCF);

  // 8. Output follows GDDRAM (not all-pixels-on test mode) [SSD1306 pg 37]
  ssd1306_display_send_command(0xA4);

  // 9. Normal (non-inverted) display [SSD1306 pg 37]
  ssd1306_display_send_command(0xA6);

  // 10. Default oscillator / clock divide [SSD1306 pg 40]
  ssd1306_display_send_command(0xD5);
  ssd1306_display_send_command(0x80);

  // 11. Enable internal charge pump -- without this the panel stays dark
  // even though the init sequence "succeeds". [SSD1306 pg 62]
  ssd1306_display_send_command(0x8D);
  ssd1306_display_send_command(0x14);

  // 12. Horizontal addressing mode + cover the whole panel so display_show()
  // can stream all 1024 bytes in one I2C write [SSD1306 pg 35]
  ssd1306_display_send_command(0x20);
  ssd1306_display_send_command(0x00);
  ssd1306_display_send_command(0x21); // column range
  ssd1306_display_send_command(0x00);
  ssd1306_display_send_command(0x7F);
  ssd1306_display_send_command(0x22); // page range
  ssd1306_display_send_command(0x00);
  ssd1306_display_send_command(0x07);

  // 13. Display on [SSD1306 pg 62]
  ssd1306_display_send_command(0xAF);

  // 14. Blank the GDDRAM and push it so we start from a known state.
  ssd1306_display_clear();
  ssd1306_display_show();
}

// Send display buffer to screen via I2C
// Must be called to actually update the display!
void ssd1306_display_show(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  i2c_write(SSD1306_DISPLAY_ADDRESS, i2c_buffer, sizeof(i2c_buffer));
}

// Clears the screen to black; no change until display_show() is called
void ssd1306_display_clear(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  memset(display_buffer, 0, SSD1306_DISPLAY_BUFFER_SIZE);
}

// Fills the display completely with white
void ssd1306_display_fill_white(void) {
  i2c_buffer[0] = 0x40; // control byte to indicate data
  memset(display_buffer, 0xFF, SSD1306_DISPLAY_BUFFER_SIZE);
}

void ssd1306_display_draw_pixel(uint16_t x, uint16_t y, color_t color) {
  // https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp#L648
  // May need to perform additional coordinate transforms,
  // depending on what coordinate system you want to use with
  // the display.

  switch (color) {
  case COLOR_WHITE:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] |= (1 << (y & 7));
    break;
  case COLOR_BLACK:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] &= ~(1 << (y & 7));
    break;
  case COLOR_INVERT:
    display_buffer[(y / 8) * SSD1306_DISPLAY_WIDTH + x] ^= (1 << (y & 7));
    break;
  }
}

void ssd1306_display_draw_horizontal_line(int16_t x_start, int16_t x_end,
                                          int16_t y, color_t color) {

  // https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp#L706

  if (y < 0 || y >= SSD1306_DISPLAY_HEIGHT) {
    return;
  }

  // Endpoints are half-open: [x_start, x_end). This matches the existing
  // draw_fill_rect which calls us as (y, y+h, ...) and expects h pixels.
  if (x_start > x_end) {
    SWAP(x_start, x_end);
  }
  if (x_start < 0) x_start = 0;
  if (x_end > SSD1306_DISPLAY_WIDTH) x_end = SSD1306_DISPLAY_WIDTH;

  for (int16_t x = x_start; x < x_end; x++) {
    ssd1306_display_draw_pixel((uint16_t)x, (uint16_t)y, color);
  }
}

void ssd1306_display_draw_vertical_line(int16_t y_start, int16_t y_end,
                                        int16_t x, color_t color) {

  // https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp#L806

  if (x < 0 || x >= SSD1306_DISPLAY_WIDTH) {
    return;
  }

  // Half-open: [y_start, y_end). See note in draw_horizontal_line.
  if (y_start > y_end) {
    SWAP(y_start, y_end);
  }
  if (y_start < 0) y_start = 0;
  if (y_end > SSD1306_DISPLAY_HEIGHT) y_end = SSD1306_DISPLAY_HEIGHT;

  for (int16_t y = y_start; y < y_end; y++) {
    ssd1306_display_draw_pixel((uint16_t)x, (uint16_t)y, color);
  }
}

void ssd1306_display_draw_fill_rect(int16_t x, int16_t y, uint16_t w,
                                    uint16_t h, color_t color) {

  // https://github.com/adafruit/Adafruit-GFX-Library/blob/master/Adafruit_GFX.cpp#L300
  for (int16_t i = x; i < x + w; i++) {
    ssd1306_display_draw_vertical_line(y, y + h, i, color);
  }
}

void ssd1306_display_draw_character_size(uint16_t x, uint16_t y,
                                         unsigned char c, color_t color,
                                         uint8_t size_x, uint8_t size_y) {

  // https://github.com/adafruit/Adafruit-GFX-Library/blob/master/Adafruit_GFX.cpp#L1249

  if ((x >= SSD1306_DISPLAY_WIDTH) ||  // Clip right
      (y >= SSD1306_DISPLAY_HEIGHT) || // Clip bottom
      ((x + 6 * size_x - 1) < 0) ||    // Clip left
      ((y + 8 * size_y - 1) < 0)) {    // Clip top
    return;
  }

  // 5 font columns (the 6th is implicit inter-character spacing — we just
  // don't draw it). Font byte = one column, bit 0 = top row.
  for (int8_t i = 0; i < 5; i++) {
    uint8_t line = pgm_read_byte(&standard_ascii_font[c * 5 + i]);
    for (int8_t j = 0; j < 8; j++, line >>= 1) {
      if (!(line & 1)) continue;
      if (size_x == 1 && size_y == 1) {
        ssd1306_display_draw_pixel(x + i, y + j, color);
      } else {
        ssd1306_display_draw_fill_rect(x + i * size_x, y + j * size_y,
                                       size_x, size_y, color);
      }
    }
  }
}
