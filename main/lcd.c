#include "lcd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"

#define LCD_ADDR 0x27

#define LCD_RS 0x01
#define LCD_EN 0x04
#define LCD_BL 0x08

static i2c_master_dev_handle_t lcd_dev;

static esp_err_t lcd_expander_write(uint8_t value)
{
    value |= LCD_BL;
    return i2c_master_transmit(lcd_dev, &value, 1, -1);
}

static esp_err_t lcd_pulse(uint8_t value)
{
    esp_err_t err = lcd_expander_write(value | LCD_EN);
    if (err != ESP_OK) return err;

    esp_rom_delay_us(1);

    err = lcd_expander_write(value & ~LCD_EN);
    if (err != ESP_OK) return err;

    esp_rom_delay_us(50);
    return ESP_OK;
}

static esp_err_t lcd_write_nibble(uint8_t nibble, uint8_t rs)
{
    uint8_t value = (nibble & 0xF0) | rs;
    return lcd_pulse(value);
}

static esp_err_t lcd_write_byte(uint8_t value, uint8_t rs)
{
    esp_err_t err;

    err = lcd_write_nibble(value & 0xF0, rs);
    if (err != ESP_OK) return err;

    return lcd_write_nibble((value << 4) & 0xF0, rs);
}

static esp_err_t lcd_command(uint8_t cmd)
{
    esp_err_t err = lcd_write_byte(cmd, 0);
    if (err == ESP_OK && (cmd == 0x01 || cmd == 0x02)) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return err;
}

esp_err_t lcd_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &lcd_dev);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(50));

    // HD44780 power-up sequence in 8-bit mode, then switch to 4-bit.
    err = lcd_write_nibble(0x30, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(5));

    err = lcd_write_nibble(0x30, 0);
    if (err != ESP_OK) return err;
    esp_rom_delay_us(150);

    err = lcd_write_nibble(0x30, 0);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(1));

    err = lcd_write_nibble(0x20, 0);
    if (err != ESP_OK) return err;

    err = lcd_command(0x28); // 4-bit, 2 lines, 5x8 font
    if (err != ESP_OK) return err;

    err = lcd_command(0x08); // display off
    if (err != ESP_OK) return err;

    err = lcd_clear();
    if (err != ESP_OK) return err;

    err = lcd_command(0x06); // entry mode
    if (err != ESP_OK) return err;

    return lcd_command(0x0C); // display on, cursor off
}

esp_err_t lcd_clear(void)
{
    return lcd_command(0x01);
}

esp_err_t lcd_home(void)
{
    return lcd_command(0x02);
}

esp_err_t lcd_set_cursor(uint8_t col, uint8_t row)
{
    static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};

    if (row > 3) return ESP_ERR_INVALID_ARG;

    return lcd_command(0x80 | (row_offsets[row] + col));
}

esp_err_t lcd_putc(char c)
{
    return lcd_write_byte((uint8_t)c, LCD_RS);
}

esp_err_t lcd_puts(const char *str)
{
    if (str == NULL) return ESP_ERR_INVALID_ARG;

    while (*str) {
        esp_err_t err = lcd_putc(*str++);
        if (err != ESP_OK) return err;
    }

    return ESP_OK;
}

static void lcd_data(uint8_t data)
{
    lcd_write_byte(
        data,
        LCD_RS
    );
}

void lcd_print(
    const char *text
)
{
    while (*text)
    {
        lcd_data(
            (uint8_t)*text
        );

        text++;
    }
}



