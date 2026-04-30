# Software Development

## Introduction


This is an ESP32 application to read sensor data from a MEMS accelerometer and convert this into wheel block heights to level a camper van.  Block heights are displayed on a webpage using an image of the van roof showing the block height needed at each wheel.

Pitch and roll angles are calculated in this application running on the ESP32.  Block heights are calculated in the HTML file based on the wheelbase and track of of the van defined in the HTML file.

HomeAssistant integration is via RESTful sensor. See HomeAssistant directory for an example YAML package and Lovelace card code.

The `/setup` page allows configuration of the Access Point (default values are located in /data/APconfig.json), Wifi network setup, sensor calibration, and firmware updates.

## Setting up the Arduino Environment

## Programming the ESP32
### Firmware OTA updates:
  Firmware .bin files are created by selecting "Sketch>Export Compiled Binary"
  The generated file will be saved under your project folder inside a Build
  folder then a series of other folders. The file with the .ino.bin extension
  is the one you should upload to your board using the ElegantOTA web page.

### LittleFS OTA updates:
  You will need the programmer conected, but not the board.
  Press [Ctrl] + [Shift] + [P] to open the command palette.
  Select "Upload Little FS to Pico/ESP8266/ESP32".
  You'll get an error because there isn't any ESP32 board connected – don't worry,
  it will still create a .bin file from the data folder.  In the debugging window you'll
  see the .littlefs.bin file location.  Probably in "C:\Users\<username>\AppData\Local\Temp\".
  
  ## Acknowledgements
  
```
Portions of this app are based on Random Nerd Tutorials
  https://randomnerdtutorials.com/
  Permission is hereby granted, free of charge, to any person obtaining a copy of this
  software and associated documentation files. The above copyright notice and this
  permission notice shall be included in all copies or substantial portions of the Software.

Portions of this app are based on Configuration-webpage-esp32 
  https://github.com/futechiot/Configuration-webpage-esp32
  GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
```