## ESP32 Wi-Fi GameBox

<table>
  <tr>
    <td><img src="imgs/pic1.jpeg"></td>
    <td><img src="imgs/pic2.jpeg"></td>
  </tr>
  <tr>
    <td><img src="imgs/wifi.png"></td>
    <td><img src="imgs/Websocket_controller.png"></td>
  </tr>
</table>

A self-contained multi-game console for the ESP32-S3: an SSD1327 OLED is the screen, and the board hosts its own Wi-Fi (`GameBox-XXXX`, password `12345678`) so any phone becomes the controller, no app needed.
Five games ship (Survivor, Descender, Blocks, Runner and 1-2 player Pong), and the page at `http://gamebox.local` (or `192.168.4.1`) morphs per game into a joystick, tilt, d-pad or buttons over a WebSocket.
Each game is a `game_module_t` in its own `.c`, decoupled from the display and network layers, so a new game is mostly one file plus a registry entry in `engine.c`.
Wire the OLED over SPI (CS 6, DC 5, RST 4, MOSI 11, SCLK 12; see [`main/hw_config.h`](main/hw_config.h)) and flash with ESP-IDF v6.0 via `idf.py build flash monitor`.
Later updates can go over Wi-Fi from the controller's ⚙ menu, but changes to the controller page still need USB.
Host unit tests run with `make -C test/host run`; the hardware checklist is in [`TEST_PLAN.md`](TEST_PLAN.md).
