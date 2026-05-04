# Software Development

## Introduction


This is an ESP32 application to read sensor data from a MEMS accelerometer and convert this into wheel block heights to level a camper van.  Block heights are displayed on a webpage using an image of the van roof showing the block height needed at each wheel.

Pitch and roll angles are calculated in this application running on the ESP32.  Block heights are calculated in the HTML file based on the wheelbase and track of of the van defined in the HTML file.

HomeAssistant integration is via RESTful sensor. See HomeAssistant directory for an example YAML package and Lovelace card code.

The `/setup` page allows configuration of the Access Point (default values are located in [data/APconfig.json](Van_Level_App/Van_Level/data/APconfig.json), Wifi network setup, sensor calibration, and firmware updates.

## Setting up the Arduino Environment
Included in the [Notes](Notes/) folder are numerous links to Arduino tutorials on the Web.  Among them are links to Rui & Sara Santos' [Random Nerd Tutorials](https://randomnerdtutorials.com/).  This application was based on their [ESP32 Web Server](https://randomnerdtutorials.com/esp32-web-server-gauges/) app.  Their pages will give you the necessary background on installing the Arduino IDE, Boards, and Libraries.  If you are not already familiar with these items, I highly recommend giving their tutorials some attention.

### Necessary Libraries
 - [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer)
 - [AsyncTCP](https://github.com/ESP32Async/AsyncTCP)
 - [Arduino_JSON](https://github.com/arduino-libraries/Arduino_JSON)
 - [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA)
 - [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO)
 - [Adafruit_Sensor](https://github.com/adafruit/Adafruit_Sensor)
 <br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;And one of the following
 - [Adafruit_ADXL343](https://github.com/adafruit/Adafruit_ADXL343)
 <br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Or
 - [Adafruit_MPU6050](https://github.com/adafruit/Adafruit_MPU6050)
 <br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;Or
- [Adafruit_MSA301](https://github.com/adafruit/Adafruit_MSA301)

### IDE Enhancements
See the *Random Nerd Tutorial* above for installation instructions
 - [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)
 
## The Van Level Program
As previously mentioned, the program is structured along the lines of *Random Nerd Tutorial's* [ESP32 Web Server](https://randomnerdtutorials.com/esp32-web-server-gauges/). The beginning starts with the global variable declarations, followed by functions to initialize our IMU sensor and retrieve data from it.

Our program sets up a local Access Point with the default SSID `VAN_LEVEL` that we will use to configure the WiFi access.  We have a single event listener to watch for Access Point connection requests and limit to a single connection.

The program actually begins in the `setup()` function where we start by setting up our local file system using LittleFS, and open the file [data/APconfig.json](Van_Level_App/Van_Level/data/APconfig.json) containing our default AP configuration information used to log into the captive Access Point.

At this point in the setup process, there is a 3-second pause to allow the `Boot` button on the ESP board to be pressed.  If pressed, this will erase the saved preferences in EEPROM.  Useful if you have customized the AP information and subsequently forgotten it.

Next, we retrieve our AP and Wifi configuration and our calibration values from the ESP32's non-volatile EEPROM storage.  This storage area is retained across restarts and power loss.  If there are no saved values, default values are substituted.

With the saved AP and Wifi information retrieved, or default values substituted, we proceed to configure our HTML query handlers.  Our Web server has two pages that it delivers to the user.  `Index` and `Setup`; both stored in the [data/](Van_Level_App/Van_Level/data/) directory.  `Index` is our default web page displaying a top-down image of a van with block heights next to the wheel locations.  `Setup` is the page where we can modify the default AP SSID and password, and/or connect to a local WiFi network, and adjust our calibration values.

One of the query handlers is a RESTful API that is the meat of our application.  We send our lightly processed IMU data as a JSON response to registered API listeners.  The raw IMU data is converted to pitch and roll angles using floating point math to take advantage of the EPS32's built-in FPU.  These pitch and roll values are processed with a low pass filter (an Exponential Moving Average), adjusted based on the calibration settings, and then served to listeners.

With our handlers in place, we establish our WiFi connection, and then sit by and listen for requests to serve pages and data.
 
## Programming the ESP32
### Initial Programming
The initial programming of the ESP32 development board will require a USB/TTL programmer like the [SH-U09C5 USB to TTL UART](https://www.amazon.com/DSD-TECH-SH-U09C5-Converter-Support/dp/B07WX2DSVB) since the Feather board does not have built-in USB support.  Once the initial program and LittleFS have been installed the first time, you can then use the application setup page and the Elegant OTA features to remote load your updates Over-The-Air.
### Firmware OTA updates:
Using the Arduino IDE, firmware updatess are created by selecting "Sketch>Export Compiled Binary". The generated file will be saved under your project folder inside a Build folder then a series of other folders. The file with the `.ino.bin` extension is the one you should upload to your board using the ElegantOTA web page.

### LittleFS OTA updates:
Again using the Arduino IDE, you will need the programmer connected, but not the board. Press `[Ctrl]` + `[Shift]` + `[P]` to open the command palette. Select `Upload Little FS to Pico/ESP8266/ESP32`. You'll get an error because there isn't any ESP32 board connected – don't worry, it will still create a .bin file from the data folder.  In the debugging window you'll see the `.littlefs.bin` file location.  Probably in "C:\Users\<username>\AppData\Local\Temp\".
  
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