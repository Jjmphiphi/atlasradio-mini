# AtlasRadio Mini — Tenstar ESP32-S3 Super Mini

Firmware fork of AtlasCube for a tiny internet radio.

## Wiring (GPIO 1–13 only)

| GPIO | Function |
|------|----------|
| 1 | I2S BCLK → MAX98357A |
| 2 | I2S LRC → MAX98357A |
| 3 | Encoder switch (JTAG_SEL; unused JTAG pins 39–42) |
| 4 | I2S DIN → MAX98357A |
| 5 | Encoder CLK |
| 6 | Encoder DT |
| 7 | Amp mute → 10 kΩ → SD_MODE |
| 8 | OLED SDA (SSD1306, 0x3C, 4.7 kΩ pull-ups) |
| 9 | OLED SCL |
| 10 | SD CS (+ 10 kΩ to 3.3 V) |
| 11 | SD MOSI |
| 12 | SD SCLK |
| 13 | SD MISO (+ 10 kΩ to 3.3 V) |

MAX98357A: VIN from board **5 V**, GAIN floating (9 dB), **SD_MODE → 470 kΩ → 5 V** for (L+R)/2. GPIO 7 LOW = mute, high-Z = unmute. Bulk **470–1000 µF** on amp 5 V.

OLED and microSD: **3.3 V only**.

## Toolchain

ESP-IDF **v5.5.4** + ESP-ADF **v2.8**. Prebuilt `lib/*.a` are locked to that ABI.

Build from this folder:

```
idf.py build
idf.py -p COMx flash monitor
```

Console is USB Serial/JTAG (GPIO 19/20), not UART0. App must stay under **1,600,000 bytes** (OTA slot is 1.625 MB); the build fails if it does not.

## First boot checks

- `esptool.py --chip esp32s3 flash_id` → 4 MB
- Log `esp_psram_get_size()` = 2097152. Boot loop → swap OCT/QUAD PSRAM in `sdkconfig.board.s3mini`
- `idf.py monitor` over USB-CDC
