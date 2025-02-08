# bussen

Made for Inkplate 5 rev2.

Before running, fill in:
- `wlanSSID`
- `wlanPass`
- `API_KEY`
- `AREA_CODE`

To build and upload:

```
arduino-cli core update-index --additional-urls https://github.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/raw/master/package_Dasduino_Boards_index.json
arduino-cli core install Inkplate_Boards:esp32
arduino-cli lib install ArduinoJson
arduino-cli lib install NTPClient
arduino-cli lib install InkplateLibrary
arduino-cli compile -e -v --fqbn Inkplate_Boards:esp32:Inkplate5V2 bussen.ino
arduino-cli upload -p /dev/ttyUSB0 -b Inkplate_Boards:esp32:Inkplate5V2 bussen.ino
```

The font was generated with:

```
fontconvert /usr/share/fonts/truetype/jetbrains-mono/JetBrainsMono-Regular.ttf 28 0 255 > font.h
```

using fontconvert from [Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)
