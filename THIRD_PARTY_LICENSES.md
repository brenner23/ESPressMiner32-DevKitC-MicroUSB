# Third-party components / Fremdkomponenten

```
The source code in this repository is MIT-licensed and was written for this
project. No third-party source code is included.

Credits / Dank:
  - SparkMiner (MIT)  https://github.com/BitzyLabs/sparkminer
    Idea only: writing the SHA text registers as buffered stores without a
    `memw` barrier. Implemented independently in C++ (src/sha256_hw.cpp).

Libraries fetched at build time (not part of this repository):
  - ArduinoJson               MIT         https://github.com/bblanchon/ArduinoJson
  - Arduino core for ESP32    LGPL-2.1    https://github.com/espressif/arduino-esp32
  - ESP-IDF                   Apache-2.0  https://github.com/espressif/esp-idf
  - pioarduino platform       Apache-2.0  https://github.com/pioarduino/platform-espressif32

Setup tools (tools/*.py) need:
  - pyserial                  BSD-3-Clause
```
