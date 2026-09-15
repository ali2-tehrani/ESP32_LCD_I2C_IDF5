#ifndef LCD_H
#define LCD_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t lcd_init(i2c_master_bus_handle_t bus);
esp_err_t lcd_clear(void);
esp_err_t lcd_home(void);
esp_err_t lcd_set_cursor(uint8_t col, uint8_t row);
esp_err_t lcd_puts(const char *str);
esp_err_t lcd_putc(char c);

void lcd_print(
    const char *text
);

#endif
