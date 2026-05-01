/*******************************************************************************************
 Van Level display

    ESP32 application to read sensor data from a MEMS accelerometer and convert this into
    wheel block heights to level a camper van.  Block heights are displayed on a webpage
    using an image of the van roof showing the block height needed at each wheel.

    Pitch and roll angles are calculated in this application running on the ESP32.
    Block heights are calculated in the HTML file based on the wheelbase and track of
    of the van defined in the HTML file.

    HomeAssistant integration is via RESTful sensor. See HomeAssistant directory for
    an example YAML package and Lovelace card code.

    The /setup page allows configuration of the Access Point (default values are located
    in /data/APconfig.json), Wifi network setup, sensor calibration, and firmware updates.

    Firmware OTA updates:
      Firmware .bin files are created by selecting "Sketch>Export Compiled Binary"
      The generated file will be saved under your project folder inside a Build
      folder then a series of other folders. The file with the .ino.bin extension
      is the one you should upload to your board using the ElegantOTA web page.

    LittleFS OTA updates:
      You will need the programmer conected, but not the board.
      Press [Ctrl] + [Shift] + [P] to open the command palette.
      Select "Upload Little FS to Pico/ESP8266/ESP32".
      You'll get an error because there isn't any ESP32 board connected – don't worry,
      it will still create a .bin file from the data folder.  In the debugging window you'll
      see the .littlefs.bin file location.  Probably in "C:\Users\<username>\AppData\Local\Temp\".

********************************************************************************************

    Portions of this app are based on Random Nerd Tutorials
      https://randomnerdtutorials.com/
      Permission is hereby granted, free of charge, to any person obtaining a copy of this
	    software and associated documentation files. The above copyright notice and this
	    permission notice shall be included in all copies or substantial portions of the Software.

    Portions of this app are based on Configuration-webpage-esp32 
      https://github.com/futechiot/Configuration-webpage-esp32
      GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

*******************************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Arduino_JSON.h>
#include <Preferences.h>
#include <Math.h>
#include <Adafruit_Sensor.h>
#include <ElegantOTA.h>

// Comment out to show plain text passwords in serial monitor
#define HIDE_PASSWORDS

// Uncomment whichever IMU sensor is connected to ESP32 I2C
//   (GPIO 23 = SDA(Qwiic blue), GPIO 22 = SCL(Qwiic yellow))
//#define MPU6050
#define ADXL343

#if defined(MPU6050)
  #include <Adafruit_MPU6050.h>
#elif defined(ADXL343)
  #include <Adafruit_ADXL343.h>
#endif

const char* VERSION = "0.9.5";

// SoftAP configuration
const char * s_configFile = "/APconfig.json"; // SoftAP configuration defaults
String wsid;
String wpass;
String esid;
String epass;
String password;
String myIP;
String localIP;
bool apConnected = false;
JSONVar configJSON;

// EEPROM storage for configuration values
Preferences preferences;

// Calibration variables
float pOffset;
int pInvert;
float rOffset;
int rInvert;

// Timer variables
unsigned long bootStart = 0;  // if non-zero, will restart when bootStart + bootDelay = now
const unsigned long bootDelay = 5000;
unsigned long lastWebTime = 0; // IMU data webpage push
const unsigned long webDelay = 1000; // refresh webpage every second
unsigned long lastSensorReadTime = 0;
unsigned long sensorReadDelay = 40000; // microseconds. 25Hz default

// IMU filtering (Exponential Moving Average)
float filteredPitch = 0.0f;
float filteredRoll = 0.0f;
const float alpha = 0.05f; // adjust this between 0.01 (heavy smoothing) and 0.9 (light smoothing)

// Create a sensor object for our MEMS accelerometer
#if defined(MPU6050)
  Adafruit_MPU6050 imuSensor;
#elif defined(ADXL343)
  Adafruit_ADXL343 imuSensor = Adafruit_ADXL343(12345);
#endif
JSONVar imuData; // json variable to hold sensor readings
bool hasIMU = false;

// Webserver pieces
AsyncWebServer server(80);
AsyncEventSource events("/events");

// Function to initialize IMU Sensor MPU-6050/ADXL343
void initSensor() {
  #if defined(MPU6050)
    Serial.println("Looking for MPU6050 IMU");
  #elif defined(ADXL343)
    Serial.println("Looking for ADXL343 IMU");
  #endif
 
  if (!imuSensor.begin()) {
    Serial.println("Could not find a valid IMU sensor, check wiring!");
    hasIMU = false;
  }
  else {
    hasIMU = true;
    #if defined(MPU6050)
      imuSensor.setAccelerometerRange(MPU6050_RANGE_2_G);
      Serial.print("Accelerometer range set to: ");
      switch (imuSensor.getAccelerometerRange()) {
        case MPU6050_RANGE_2_G:
          Serial.print("±2");
          break;
        case MPU6050_RANGE_4_G:
          Serial.print("±4");
          break;
        case MPU6050_RANGE_8_G:
          Serial.print("±8");
          break;
        case MPU6050_RANGE_16_G:
          Serial.print("±16");
          break;
        default:
          Serial.print("??");
          break;
      }
      Serial.println("G");

      imuSensor.setFilterBandwidth(MPU6050_BAND_21_HZ);
      Serial.print("Filter bandwidth set to: ");
      // confirm data rate and sync polling delay
      switch (imuSensor.getFilterBandwidth()) {
        case MPU6050_BAND_260_HZ:
          Serial.print("260");
          sensorReadDelay = 1000000/260;
          break;
        case MPU6050_BAND_184_HZ:
          Serial.print("184");
          sensorReadDelay = 1000000/184;
          break;
        case MPU6050_BAND_94_HZ:
          Serial.print("94");
          sensorReadDelay = 1000000/94;
          break;
        case MPU6050_BAND_44_HZ:
          Serial.print("44");
          sensorReadDelay = 1000000/44;
          break;
        case MPU6050_BAND_21_HZ:
          Serial.print("21");
          sensorReadDelay = 1000000/21;
          break;
        case MPU6050_BAND_10_HZ:
          Serial.print("10");
          sensorReadDelay = 1000000/10;
          break;
        case MPU6050_BAND_5_HZ:
          Serial.print("5");
          sensorReadDelay = 1000000/5;
          break;
        default:
          Serial.println("??");
          break;
      }
      Serial.println("Hz");

    #elif defined(ADXL343)
      imuSensor.setRange( ADXL343_RANGE_2_G);
      Serial.print( "Accelerometer range set to: ");
      switch ( imuSensor.getRange()) {
        case ADXL343_RANGE_2_G:
          Serial.print("±2");
          break;
        case ADXL343_RANGE_4_G:
          Serial.print("±4");
          break;
        case ADXL343_RANGE_8_G:
          Serial.print("±8");
          break;
        case ADXL343_RANGE_16_G:
          Serial.print("±16");
          break;
        default:
          Serial.print("??");
          break;
      }
      Serial.println("G");
      
      imuSensor.setDataRate( ADXL343_DATARATE_25_HZ);
      Serial.print( "Data rate set to: ");
      // confirm data rate and sync polling delay
      switch(imuSensor.getDataRate()) {
        case ADXL343_DATARATE_3200_HZ:
          Serial.print("3200");
          sensorReadDelay = 1000000/3200;
          break;
        case ADXL343_DATARATE_1600_HZ:
          Serial.print("1600");
          sensorReadDelay = 1000000/1600;
          break;
        case ADXL343_DATARATE_800_HZ:
          Serial.print("800");
          sensorReadDelay = 1000000/800;
          break;
        case ADXL343_DATARATE_400_HZ:
          Serial.print("400");
          sensorReadDelay = 1000000/400;
          break;
        case ADXL343_DATARATE_200_HZ:
          Serial.print  ("200");
          sensorReadDelay = 1000000/200;
          break;
        case ADXL343_DATARATE_100_HZ:
          Serial.print("100");
          sensorReadDelay = 1000000/100;
          break;
        case ADXL343_DATARATE_50_HZ:
          Serial.print("50");
          sensorReadDelay = 1000000/50;
          break;
        case ADXL343_DATARATE_25_HZ:
          Serial.print("25");
          sensorReadDelay = 1000000/25;
          break;
        case ADXL343_DATARATE_12_5_HZ:
          Serial.print("12.5");
          sensorReadDelay = 1000000/12.5;
          break;
        case ADXL343_DATARATE_6_25HZ:
          Serial.print("6.25");
          sensorReadDelay = 1000000/6.25;
          break;
        case ADXL343_DATARATE_3_13_HZ:
          Serial.print("3.13");
          sensorReadDelay = 1000000/3.13;
          break;
        case ADXL343_DATARATE_1_56_HZ:
          Serial.print("1.56");
          sensorReadDelay = 1000000/1.56;
          break;
        case ADXL343_DATARATE_0_78_HZ:
          Serial.print("0.78");
          sensorReadDelay = 1000000/0.78;
          break;
        case ADXL343_DATARATE_0_39_HZ:
          Serial.print("0.39");
          sensorReadDelay = 1000000/0.39;
          break;
        case ADXL343_DATARATE_0_20_HZ:
          Serial.print("0.20");
          sensorReadDelay = 1000000/0.2;
          break;
        case ADXL343_DATARATE_0_10_HZ:
          Serial.print("0.10");
          sensorReadDelay = 1000000/0.1;
          break;
        default:
          Serial.print("??");
          break;
      }
      Serial.println("Hz");
    #endif
  }
}

// Function to constantly poll the IMU and apply the EMA filter
void updateSensorData() {
  if (hasIMU) {
    float rawPitch = 0; // changed from double
    float rawRoll = 0;  // changed from double

    #if defined(MPU6050)
      sensors_event_t a, g, temp;
      imuSensor.getEvent(&a, &g, &temp);
      
      // rawPitch = (atan(a.acceleration.x / sqrt(sq(a.acceleration.y) + sq(a.acceleration.z)))) * 180/PI;
      // rawRoll = (atan(a.acceleration.y / sqrt(sq(a.acceleration.x) + sq(a.acceleration.z)))) * 180/PI;
      
      // optimized float calculations instead of doubles above to utilize the ESP32 hardware floating point unit
      rawPitch = atan2f(a.acceleration.x, sqrtf(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 57.29577951f;
      rawRoll = atan2f(a.acceleration.y, sqrtf(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 57.29577951f;
      
    #elif defined(ADXL343)
      sensors_event_t event;
      imuSensor.getEvent(&event);
      
      // rawPitch = (atan(event.acceleration.x / sqrt(sq(event.acceleration.y) + sq(event.acceleration.z)))) * 180/PI;
      // rawRoll = (atan(event.acceleration.y / sqrt(sq(event.acceleration.x) + sq(event.acceleration.z)))) * 180/PI;
      
      // optimized float calculations instead of doubles above to utilize the ESP32 hardware floating point unit
      rawPitch = atan2f(event.acceleration.x, sqrtf(event.acceleration.y * event.acceleration.y + event.acceleration.z * event.acceleration.z)) * 57.29577951f;
      rawRoll = atan2f(event.acceleration.y, sqrtf(event.acceleration.x * event.acceleration.x + event.acceleration.z * event.acceleration.z)) * 57.29577951f;
    #endif

    // apply the Exponential Moving Average (EMA) filter
    filteredPitch = (alpha * rawPitch) + ((1.0f - alpha) * filteredPitch);
    filteredRoll = (alpha * rawRoll) + ((1.0f - alpha) * filteredRoll);
  }
}

// Function to grab the latest filtered data, apply calibration, and output JSON
String getSensorReadings() {
  float finalPitch = 0;
  float finalRoll = 0;

  if (hasIMU) {
    // apply calibration values to the continuously filtered data
    finalPitch = (filteredPitch + pOffset) * pInvert;
    finalRoll = (filteredRoll + rOffset) * rInvert;
  }

  imuData["pitch"] = String(finalPitch, 1);
  imuData["roll"] = String(finalRoll, 1);
  imuData["ver"] = String(VERSION);

  Serial.print("Filtered Pitch: ");
  Serial.print(finalPitch);
  Serial.print(", Filtered Roll: ");
  Serial.print(finalRoll);
  Serial.println("");

  String jsonString = JSON.stringify(imuData);
  return jsonString;
}

// Function to test WiFi connection reverting to SoftAP if unsuccessful
bool testWifi(void) {
  int c = 0;
  Serial.print("Waiting for Wifi to connect. Status: ");
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(wsid.c_str(), wpass.c_str());

  while (c < 30) {
    Serial.print(WiFi.status());
    Serial.print(",");
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("\nYou are connected to: ");
      localIP = WiFi.localIP().toString();
      Serial.println(localIP);
      return true;
    }
    delay(500);
    c++;
  }

  Serial.println("");
  Serial.println("STA connect timed out, opening SoftAP");
  localIP = "Not Connected";
  WiFi.mode(WIFI_AP);
  return false;
}

// Function to watch for SoftAP connection events
//   We only want one SoftAP connection. For basic security, we rely
//   on the AP password to limit who can change our configuration.
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      apConnected = false;
      Serial.println("SoftAP client disconnected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      // save the IP to compare to config page requestor
      apConnected = true;
      Serial.println("SoftAP client connected");
      break;
    default:
      break;
  }
}

//####################################  BEGIN SETUP  ##################################
void setup() {
  Serial.begin(115200);
  initSensor();

  // connect to EEPROM storage
  if (!preferences.begin("config", false)) {
    Serial.println("Failed to initialise Preferences");
    while (1);
  }
  Serial.println("Preferences initialized");

  // mount our file server for our static data and web pages
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
    while (1);
  }
  Serial.println("LittleFS successfully mounted");

  // get default SoftAP configuration from file
  File dataFile = LittleFS.open(s_configFile, FILE_READ); //Open File for reading
  if (!dataFile) {
    Serial.println("Config file open failed on read.");
    while (1);
  } else {
    String configContents;
    Serial.println("Reading configuration data from file:");
    if (dataFile.available()) configContents = dataFile.readString();
    dataFile.close();
    #ifdef HIDE_PASSWORDS
      Serial.println( "*Hidden*");
    #else
      Serial.println(configContents);
    #endif

    configJSON = JSON.parse(configContents);
    if (JSON.typeof(configJSON) == "undefined") {
      Serial.println("Parsing Config failed!");
      while (1);
    }
  }

  // reset board to defaults if necessary (i.e. lost password)
  //  click EN/RESET button, then press and hold BOOT/GPIO0 button for 5 seconds
  //  careful with Adafruit Huzzah ESP32 breakout board, it does not come with 
  //  pull-up on GPIO0 like the Devkit V1 boards - you need to add it, or use INPUT_PULLUP.
  //  select the INPUT version below if you have an external pullup resistor.
  pinMode( 0, INPUT_PULLUP);
  //pinMode( 0, INPUT);
 
  Serial.println( "Hold \"BOOT\" button to reset board to defaults");
  delay(3000);
  if (digitalRead( 0) == LOW) { // button is pulled high on hardware, pressing it makes it low
    Serial.println( "Resetting, release \"BOOT\" button now");
    preferences.clear();
  }

  // retrieve EEPROM sensor calibration values
  pOffset = preferences.getFloat("pOffset", 0.0);
  rOffset = preferences.getFloat("rOffset", 0.0);
  pInvert = preferences.getInt("pInvert", 1);
  rInvert = preferences.getInt("rInvert", 1);
  Serial.print("Stored Calibration values: pOffset=");
  Serial.print(pOffset);
  Serial.print(" pInvert=");
  Serial.print(pInvert);
  Serial.print(" rOffset=");
  Serial.print(rOffset);
  Serial.print(" rInvert=");
  Serial.println(rInvert);

  // retrieve EEPROM AP configuration
  esid = preferences.getString("AP_SSID", "");
  epass = preferences.getString("AP_PASS", "");
  String eap = preferences.getString("AP_IP", "");

  // if EEPROM info is missing, use defaults from config file
  if ((esid == "") && (epass == "")) {
    Serial.println("\nInitial SoftAP user/pass configuration");
    esid = String(configJSON["AP_name"]);
    epass = String(configJSON["AP_pass"]);
    preferences.putString("AP_SSID", esid);
    preferences.putString("AP_PASS", epass);
  }
  if (eap == "") {
    Serial.println("\nInitial SoftAP address configuration");
    eap = String(configJSON["AP_IP"]);
    preferences.putString("AP_IP", eap);
  }

  Serial.print("Access point SSID: ");
  Serial.println(esid);
  Serial.print("Access point PASSWORD: ");
  #ifdef HIDE_PASSWORDS
    Serial.println( "*Hidden*");
  #else
    Serial.println(epass);
  #endif
  Serial.print("Access point ADDRESS: ");
  Serial.println(eap);

  // start our SoftAP with one allowed connection
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPsetHostname(esid.c_str());
  if (!WiFi.softAP(esid.c_str(), epass.c_str(), 1, 0, 1))
  {
    Serial.println("SoftAP creation failed.");
    while (1);
  }
  delay(100);

  IPAddress apIP;
  apIP.fromString(eap);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, NMask);
  myIP = WiFi.softAPIP().toString();
  Serial.print("SoftAP server started at: ");
  Serial.println(myIP);

  // HTML+JS+CSS+Images server callbacks
  // handlers are evaluated in the order they are attached to the server.
  server.onNotFound([](AsyncWebServerRequest * request) {
    request -> send(404, "text/html", "<h1 align=\"center\">Oops! It's not here.</h1>");
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    // our van level image page with blocking stack heights
    request -> send(LittleFS, "/index.html", "text/html");
  });

  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest * request) {
    if(!request->authenticate(esid.c_str(), epass.c_str()))
      return request->requestAuthentication();
    request -> send(LittleFS, "/setup.html", "text/html");
  });

  server.on("/setup.html", HTTP_GET, [](AsyncWebServerRequest * request) {
    if(!request->authenticate(esid.c_str(), epass.c_str()))
      return request->requestAuthentication();
    request -> send(LittleFS, "/setup.html", "text/html");
  });

  // server callback for RESTful IMU update readings
  server.on( "/readings", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println( "RESTful query");
    String json = getSensorReadings();
    request->send( 200, "application/json", json);
  });

  // server callback button handlers
  server.on("/setupData", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println( "Fetching Offsets");
    String content = String("{\"ver\":\"") + VERSION + "\",\"myIP\":\"" + myIP + "\",\"localIP\":\"" + localIP
                      + "\",\"wsid\":\"" + wsid + "\",\"apname\":\"" + esid
                      + "\",\"p_offset\":\"" + pOffset + "\",\"p_invert\":\"" + pInvert
                      + "\",\"r_offset\":\"" + rOffset + "\",\"r_invert\":\"" + rInvert
                      + "\",\"MAC\":\""+ String( WiFi.macAddress()) + "\"}";
    Serial.println(content);
    request -> send(200, "application/json", content);
  });

  server.on("/scanWifi", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println( "Scanning Wifi");
    String scan_wifi = request -> getParam("scan_wifi") -> value();
    if (scan_wifi) {
      Serial.println("scan_wifi");
      String json = "[";
      int n = WiFi.scanNetworks();
      if (n == 0) {
        Serial.println("no networks found");
      } else {
        Serial.print(n);
        Serial.println(" networks found");
        for (int i = 0; i < n; ++i) {
          // print SSID and RSSI for each network found
          if (i)
            json += ", ";
          json += " {";
          json += "\"rssi\":" + String(WiFi.RSSI(i));
          json += ",\"ssid\":\"" + WiFi.SSID(i) + "\"";
          json += ",\"bssid\":\"" + WiFi.BSSIDstr(i) + "\"";
          json += ",\"channel\":" + String(WiFi.channel(i));
          json += ",\"secure\":" + String(WiFi.encryptionType(i));
          json += "}";
          if (i == (n - 1)) {
            json += "]";
          }
        }
        delay(100);
        Serial.println(json);
        request -> send(200, "application/json", json);
      }
    }
  });

  server.on("/applyBtnFunction", HTTP_POST, [](AsyncWebServerRequest * request) {
    Serial.println( "Assigning AP");
    String txtssid = request -> getParam("txtssid", true) -> value();
    String txtpass = request -> getParam("txtpass", true) -> value();
    String txtaplan = request -> getParam("txtaplan", true) -> value();

    if (txtssid.length() > 0) {
      preferences.putString("AP_SSID", txtssid);
      Serial.print("Writing AP SSID :: ");
      Serial.println(txtssid);
    }
    // WPA2 passwords must be 8 or more characters. SoftAP will fail if fewer.
    if (txtpass.length() > 7) {
      preferences.putString("AP_PASS", txtpass);
      Serial.print("Writing AP PASSWORD :: ");
      #ifdef HIDE_PASSWORDS
        Serial.println( "*Hidden*");
      #else
        Serial.println(txtpass);
      #endif
    } else {
      Serial.print("New AP Password, \"");
      Serial.print(txtpass);
      Serial.println("\", too short, not saved");
    }
    if (txtaplan.length() > 0) {
      preferences.putString("AP_IP", txtaplan);
      Serial.print("Writing AP IP :: ");
      Serial.println(txtaplan);
    }
    request -> send(200, "text/plain", "ok");
  });

  server.on("/connectBtnFunction", HTTP_POST, [](AsyncWebServerRequest * request) {
    Serial.println( "Assigning Wifi");
    String wifi_ssid = request -> getParam("wifi_ssid", true) -> value();
    Serial.println(wifi_ssid);
    String wifi_pass = request -> getParam("wifi_pass", true) -> value();
    #ifdef HIDE_PASSWORDS
      Serial.println( "*Hidden*");
    #else
      Serial.println(wifi_pass);
    #endif
    String wifi_mode = request -> getParam("wifi_mode", true) -> value();
    Serial.println(wifi_mode);

    if (wifi_ssid.length() > 0) {
      preferences.putString("WIFI_SSID", wifi_ssid);
      Serial.print("Writing WIFI SSID :: ");
      Serial.println(wifi_ssid);
    }
    if (wifi_pass.length() > 7) { // WPA2 requires a least 8 chars
      preferences.putString("WIFI_PASS", wifi_pass);
      Serial.print("Writing WIFI PASSWORD :: ");
      #ifdef HIDE_PASSWORDS
        Serial.println( "*Hidden*");
      #else
        Serial.println(wifi_pass);
      #endif
    } else {
      Serial.print("New WiFi Password, \"");
      Serial.print(wifi_pass);
      Serial.println("\", too short, not saved");
    }

    // writing WIFI settings to EEPROM
    preferences.putString("WIFI_MODE", wifi_mode);
    Serial.print("Writing WIFI MODE :: ");
    Serial.println(wifi_mode);

    if (wifi_mode == "static") {
      String txtipadd = request -> getParam("txtipadd") -> value();
      String net_m = request -> getParam("net_m") -> value();
      String g_add = request -> getParam("g_add") -> value();
      String p_dns = request -> getParam("p_dns") -> value();
      String s_dns = request -> getParam("s_dns") -> value();

      if (txtipadd.length() > 0) {
        preferences.putString("WIFI_IP", txtipadd);
        Serial.print("Writing WIFI IP :: ");
        Serial.println(txtipadd);
      }
      if (net_m.length() > 0) {
        preferences.putString("WIFI_MASK", net_m);
        Serial.print("Writing WIFI MASK :: ");
        Serial.println(net_m);
      }
      if (g_add.length() > 0) {
        preferences.putString("WIFI_GATE", g_add);
        Serial.print("Writing WIFI GATEWAY :: ");
        Serial.println(g_add);
      }
      if (p_dns.length() > 0) {
        preferences.putString("WIFI_PDNS", p_dns);
        Serial.print("Writing WIFI PDNS :: ");
        Serial.println(p_dns);
      }
      if (s_dns.length() > 0) {
        preferences.putString("WIFI_SDNS", s_dns);
        Serial.print("Writing WIFI SDNS :: ");
        Serial.println(s_dns);
      }
    }
    request -> send(200, "text/plain", "ok");
  });

  server.on("/applyOffsetFunction", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println( "Assigning Offsets");
    // if unable to parse the offset values, the variables will contain 0 which is okay
    pOffset = (request -> getParam("p_offset") -> value()).toFloat();
    rOffset = (request -> getParam("r_offset") -> value()).toFloat();
 
    // the invert variables will be 0 on failure and those need to be change to 1
    pInvert = (request -> getParam("p_invert") -> value()).toInt();
    rInvert = (request -> getParam("r_invert") -> value()).toInt();

    if (pInvert < 0) {
      pInvert = -1;
    } else {
      pInvert = 1;
    }
    if (rInvert < 0) {
      rInvert = -1;
    } else {
      rInvert = 1;
    }

    preferences.putFloat("pOffset", pOffset);
    preferences.putFloat("rOffset", rOffset);
    preferences.putInt("pInvert", pInvert);
    preferences.putInt("rInvert", rInvert);

    Serial.print("Calibration values set: pOffset=");
    Serial.print(pOffset);
    Serial.print(" pInvert=");
    Serial.print(pInvert);
    Serial.print(" rOffset=");
    Serial.print(rOffset);
    Serial.print(" rInvert=");
    Serial.println(rInvert);
    request -> send(200, "text/plain", "ok");
  });

  server.on("/rebootBtnFunction", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println( "Requesting Restart");
    if (request -> getParam("reboot_btn") -> value() == "reboot_device") {
      Serial.println("restarting device");
      request -> send(200, "text/plain", "ok");
      // handle rebooting in the loop() - watchdog gets upset if we block with Delay() here.
      bootStart = millis();
    }
  });

  server.on("/resetBtnFunction", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println( "Requesting Reset");
    if (request -> getParam("reset_btn") -> value() == "reset_device") {
      preferences.clear();
      Serial.println("EEPROM cleared");
      request -> send(200, "text/plain", "ok");
    }
  });

  // now handle everything else (images, css, javascript)
  server.serveStatic("/", LittleFS, "/");

  // server event callback
  events.onConnect([](AsyncEventSourceClient * client) {
    if (client -> lastId()) {
      Serial.printf("Client reconnected! Last message ID that it got is: %lu\n", client -> lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client -> send("hello!", NULL, millis(), 10000);
  });
  server.addHandler( & events);

  // setup our Wifi connection
  wsid = preferences.getString("WIFI_SSID", "");
  Serial.print("\nWifi SSID: ");
  Serial.println(wsid);

  wpass = preferences.getString("WIFI_PASS", "");
  Serial.print("Wifi PASSWORD: ");
  #ifdef HIDE_PASSWORDS
    Serial.println( "*Hidden*");
  #else
    Serial.println(wpass);
  #endif

  String wmode = preferences.getString("WIFI_MODE", "");
  Serial.print("Wifi MODE: ");
  Serial.println(wmode);

  if (wsid == NULL) {
    wsid = "Not Given";
    localIP = "network not set";
  }

  if (wmode == "dhcp") {
    Serial.println("Obtaining DHCP address");
    testWifi();
  }

  if (wmode == "static") {
    String static_ip = preferences.getString("WIFI_IP", "");
    Serial.println("Using WiFi static address: ");
    Serial.print("static_ip: ");
    Serial.println(static_ip);

    String sub_net = preferences.getString("WIFI_MASK", "");
    Serial.print("sub_net: ");
    Serial.println(sub_net);

    String g_add = preferences.getString("WIFI_GATE", "");
    Serial.print("gateway: ");
    Serial.println(g_add);

    String p_dns = preferences.getString("WIFI_PDNS", "");
    Serial.print("pri-dns: ");
    Serial.println(p_dns);

    String s_dns = preferences.getString("WIFI_SDNS", "");
    Serial.print("sec-dns: ");
    Serial.println(s_dns);

    IPAddress s_ip, gateway, subnet, primaryDNS, secondaryDNS;
    s_ip.fromString(static_ip);
    gateway.fromString(g_add);
    subnet.fromString(sub_net);
    primaryDNS.fromString(p_dns);
    secondaryDNS.fromString(s_dns);

    if (!WiFi.config(s_ip, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("STA Failed to configure");
    }

    testWifi();
  }

  server.begin(); // start the webserver for the Access Point and Station
  
  // start the OTA service using our SoftAP credentials
  ElegantOTA.begin(&server);
  ElegantOTA.setAuth(esid.c_str(), epass.c_str());
}

//####################################  BEGIN LOOP  ##################################
void loop() {
  ElegantOTA.loop();
  
  if (bootStart != 0) {
    // received a reboot request from the setup page
    if ((millis() - bootStart) > bootDelay) {
      ESP.restart();
    }
  }
  
  // poll IMU at the sensor bandwidth rate
  if ((micros() - lastSensorReadTime) >= sensorReadDelay) {
    updateSensorData();
    lastSensorReadTime = micros();
  }
  
  // update the index page with latest sensor readings
  if ((WiFi.status() == WL_CONNECTED) || apConnected) {
     if ((millis() - lastWebTime) >= webDelay) {
      events.send("ping", NULL, millis());
      events.send(getSensorReadings().c_str(), "new_readings", millis());
      lastWebTime = millis();
    }
  }
  
  // attempt to reconnect to WiFi if disconnected for any reason
  if (wsid.length() > 0 && wpass.length() > 0 && WiFi.status() != WL_CONNECTED) {
    testWifi();
  }
}
