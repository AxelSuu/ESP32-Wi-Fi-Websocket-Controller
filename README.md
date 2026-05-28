## ESP32 Wi-Fi Websocket controller

This is an embedded wireless Wi-Fi Websocket controller written in C using esp-idf. a Wi-Fi softAP, an SPIFFS-hosted web UI, and a WebSocket server for real-time control. It combines low-level peripheral access (SPI display) with networking and filesystem services to build an interactive embedded web application. Running on http://192.168.4.1 with Wi-Fi password 12345678

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3-DevKitC-1 (8 MB flash, 8 MB PSRAM) |
| Display | SSD1327 OLED, 128×96 pixels, 4-bit grayscale, SPI |

**SPI pin assignments:**

| GPIO | Function |
|------|----------|
| 6 | CS |
| 5 | DC |
| 4 | RST |
| 11 | MOSI |
| 12 | SCLK |

<table>
  <tr>
    <td><img src="imgs/pic1.jpeg"></td>
    <td><img src="imgs/pic2.jpeg"></td>
  </tr>
</table>

<table>
  <tr>
    <td><img src="imgs/wifi.png"></td>
    <td><img src="imgs/Websocket_controller.png"></td>
  </tr>
</table>

## Build and Flash

```bash
# Source the ESP-IDF environment
. $IDF_PATH/export.sh

idf.py build
idf.py flash monitor
```


## Architecture

- **Wi-Fi**: softAP mode.
- **HTTP server**: `esp_http_server` on port 80. Serves `index.html` from SPIFFS and handles `/start` and `/ws` endpoints.
- **WebSocket**: URI `/ws` on port 80. Receives `{"action":"move","dir":"up"/"down"}` messages and forwards them to the game logic.
- **Display**: full 6144-byte framebuffer (128×96 / 2 bytes per pixel) flushed to the SSD1327 via a single SPI DMA transaction each frame.
- **Game loop**: FreeRTOS task on core 0, 30 ms ticks (`~33 FPS`). Game state is protected by a mutex shared with the WebSocket handler.
