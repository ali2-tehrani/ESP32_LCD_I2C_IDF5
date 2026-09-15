ESP32 + LCD 16x2 I2C - ESP-IDF 5.x

Hardware:
  ESP32 GPIO21 -> LCD SDA
  ESP32 GPIO22 -> LCD SCL
  ESP32 GND    -> LCD GND
  ESP32 5V     -> LCD VCC

Default I2C address:
  0x27

If your backpack uses 0x3F, change LCD_ADDR in main/lcd.c.

Build:
  idf.py set-target esp32
  idf.py build

Flash and monitor:
  idf.py -p COMx flash monitor

This project does NOT use smbus.h or the old driver/i2c.h API.
It uses the ESP-IDF 5.x driver/i2c_master.h API.
