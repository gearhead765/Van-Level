/*******************************************************************************************
 Van Level display

    ESP32 application to read sensor data from a MEMS accelerometer and convert this into
    wheel block heights to level a camper van.  Block heights are displayed on a webpage
    (index.html) showing an image of the van roof with the block height needed at each wheel.

    This App also uses ESP-NOW to communicate with other ESP32 devices that can detect
    obstacles and display the information on the van level webpage.  There are three regions
    for obstical detection.  Forward and Rearward detection zones use LiDAR sensors to
    determine if there is an obsticle that will strike the roof mounted airconditioner.
    An addtional detection zone is used for underneath the van; an ultrasonic sensor is
    used determine the clearance underneath the grey water tank drain valve.

    Obstical detection zones use color coding to indicate the presence of obstacles.
    Green indicates no obsticles within range.  Red indicates the presence of an obstacle,
    and yellow indicates a problem with the sensor or radio communication.  Zones will also
    display a range value to the obstacle detected.

    The /setup page allows configuration of the Access Point, Wifi network setup, sensor
    calibration, camper information and firmware updates.  Default AP credentials are located
    in the LittleFS data folder at /data/APconfig.json.  You can change these if you want, but
    the prefered method is to change the AP credentials after firmware installation using the
    /setup page.  If you forget your AP credentials, you can recover the defaults by holding down
    the boot button on the ESP32 for a 5 seconds during startup.

    Wifi connections are managed through the /setup page, where you can configure the ESP32
    to connect to your preferred network.  Once connected, your preferences will be saved and
    used for future connections.

    There are two methods for integrating with HomeAssistant.  The easiest is just to embed
    the webpage in a HomeAssistant panel using a Webpage Card (i.e., an iframe).  This
    provides you with dark mode support background coloring to show level, and automatic
    landscape/portrait orientation support.  The limitation for this method is that ou can’t
    embed sites using HTTP if you are using HTTPS for your Home Assistant, nor can you use
    the leveling information in HA automations.

    The second method for HomeAssistant integration is via RESTful sensor and a Picture Card.
    See HomeAssistant directory for an example YAML package and Lovelace card code. This method
    only supports landscape orientation, does not support Dark mode, and will not recolor the
    background based on level.  This method works with HTTPS based setups, and allows the 
    leveling information like wheel lift amounts or "is level" to be used in automations.

 
    Over The Air updates are handled by ElegantOTA https://github.com/ayushsharma82/ElegantOTA.

    Firmware OTA updates:
      Firmware .bin files are created by selecting "Sketch>Export Compiled Binary"
      The generated file will be saved under your project folder inside a Build
      folder then a series of other folders. The file with the .ino.bin extension
      is the one you should upload to your board using the ElegantOTA web page.

    LittleFS OTA updates:
      You will need the programmer connected, but not the board.
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

*******************************************************************************************/
const char* VERSION = "2.0.0a";

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <cstring>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Arduino_JSON.h>
#include <Preferences.h>
#include <Math.h>
#include <Adafruit_Sensor.h>
#include <ElegantOTA.h>

#define DEBUG_SERIAL 0
#if DEBUG_SERIAL
  #define DBG_PRINT(...) Serial.print(__VA_ARGS__)
  #define DBG_PRINTLN(...) Serial.println(__VA_ARGS__)
  #define DBG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG_PRINT(...) do {} while (0)
  #define DBG_PRINTLN(...) do {} while (0)
  #define DBG_PRINTF(...) do {} while (0)
#endif

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

// Calibration variables - reasonable defaults here, configurable on setup page
float pOffset = 0.0f;
int pInvert = 1;
float rOffset = 0.0f;
int rInvert = 1;
float pTolerance = 0.15f;    // "level" if within tolerance degrees
float rTolerance = 0.15f;
float blockFraction = 2.0f;  // default to 1/2" block heights
float wheelbase = 148.0f;    // inches
float track = 68.0f;         // inches
String vanType = "Transit";

// Timer variables
unsigned long bootStart = 0;  // if non-zero, will restart when bootStart + bootDelay = now
const unsigned long bootDelay = 5000;
unsigned long lastWebTime = 0;      // IMU data webpage push
const unsigned long webDelay = 200;
unsigned long lastSensorReadTime = 0;
unsigned long sensorReadDelay = 40000; // microseconds - 25Hz default
const unsigned long staRetryInterval = 10000; // retry saved STA credentials this often while disconnected
unsigned long lastStaRetryAttempt = 0;
volatile bool wifiApplyPending = false; // set by the setup page; loop() reloads the saved WiFi settings

// Webserver pieces
AsyncWebServer server(80);
AsyncEventSource events("/events");

// The AP stays up in AP+STA mode at all times so setup is always reachable;
// the STA side retries the saved credentials indefinitely.
String wifiStatusText = "Starting up";

void setWifiStatusText(const String &status) {
  if (wifiStatusText != status) {
    wifiStatusText = status;
    DBG_PRINTLN("WiFi status: " + wifiStatusText);
  }
}

// IMU filtering (Exponential Moving Average)
float filteredPitch = 0.0f;
float filteredRoll = 0.0f;
float alpha = 0.1f; // setup configurable - 0.01 (heavy smoothing) and 0.9 (light smoothing)

JSONVar imuData; // only touched by loop(); web handlers read latestReadings instead
String latestReadings = "{}"; // most recent JSON published by loop()
SemaphoreHandle_t readingsMutex;
bool hasIMU = false;

// ESP-NOW message handling for distance sensors
enum DistanceUnits : uint8_t {
  UNITS_CM = 0,
  UNITS_INCH = 1,
  UNITS_M = 2,
  UNITS_FT = 3
};

typedef struct {
  uint8_t id;
  uint8_t units;
  int16_t distance;
} EspNowMessage;

struct EspNowReading {
  int16_t distance;
  uint8_t units;
  unsigned long receivedAt;
  bool received;
};

EspNowReading espNowReadings[256] = {};
portMUX_TYPE espNowMutex = portMUX_INITIALIZER_UNLOCKED;
const unsigned long espNowStaleDelay = 2000;

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int length) {
  if (data == nullptr || length != sizeof(EspNowMessage)) return;

  EspNowMessage message;
  memcpy(&message, data, sizeof(message));
  unsigned long receivedAt = millis();
  portENTER_CRITICAL(&espNowMutex);
  espNowReadings[message.id].distance = message.distance;
  espNowReadings[message.id].units = message.units;
  espNowReadings[message.id].receivedAt = receivedAt;
  espNowReadings[message.id].received = true;
  portEXIT_CRITICAL(&espNowMutex);
}

void initEspNow() {
  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW initialization failed: %d\n", result);
    return;
  }
  result = esp_now_register_recv_cb(onEspNowReceive);
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW receive callback registration failed: %d\n", result);
    esp_now_deinit();
    return;
  }
  Serial.printf("ESP-NOW listening on STA MAC %s, WiFi channel %d\n",
                WiFi.macAddress().c_str(), WiFi.channel());
}

JSONVar getEspNowReadings() {
  JSONVar readings = JSON.parse("[]");
  int index = 0;
  for (int id = 0; id < 256; ++id) {
    portENTER_CRITICAL(&espNowMutex);
    EspNowReading reading = espNowReadings[id];
    portEXIT_CRITICAL(&espNowMutex);
    if (!reading.received) continue;

    unsigned long age = millis() - reading.receivedAt;
    readings[index]["id"] = id;
    readings[index]["distance"] = reading.distance;
    readings[index]["units"] = reading.units;
    readings[index]["ageMs"] = age;
    readings[index]["stale"] = age >= espNowStaleDelay;
    ++index;
  }
  return readings;
}

// Create a sensor object for our MEMS accelerometer
#if defined(MPU6050)
  Adafruit_MPU6050 imuSensor;
#elif defined(ADXL343)
  Adafruit_ADXL343 imuSensor = Adafruit_ADXL343(12345);
#endif

// Function to initialize IMU Sensor MPU-6050/ADXL343
void initSensor() {
  #if defined(MPU6050)
    DBG_PRINTLN("Looking for MPU6050 IMU");
  #elif defined(ADXL343)
    DBG_PRINTLN("Looking for ADXL343 IMU");
  #endif
 
  if (!imuSensor.begin()) {
    DBG_PRINTLN("Could not find a valid IMU sensor, check wiring!");
    hasIMU = false;
  }
  else {
    hasIMU = true;
    #if defined(MPU6050)
      imuSensor.setAccelerometerRange(MPU6050_RANGE_2_G);
      DBG_PRINT("Accelerometer range set to: ");
      switch (imuSensor.getAccelerometerRange()) {
        case MPU6050_RANGE_2_G:
          DBG_PRINT("±2");
          break;
        case MPU6050_RANGE_4_G:
          DBG_PRINT("±4");
          break;
        case MPU6050_RANGE_8_G:
          DBG_PRINT("±8");
          break;
        case MPU6050_RANGE_16_G:
          DBG_PRINT("±16");
          break;
        default:
          DBG_PRINT("??");
          break;
      }
      DBG_PRINTLN("G");

      imuSensor.setFilterBandwidth(MPU6050_BAND_21_HZ);
      DBG_PRINT("Filter bandwidth set to: ");
      // confirm data rate and sync polling delay
      switch (imuSensor.getFilterBandwidth()) {
        case MPU6050_BAND_260_HZ:
          DBG_PRINT("260");
          sensorReadDelay = 1000000/260;
          break;
        case MPU6050_BAND_184_HZ:
          DBG_PRINT("184");
          sensorReadDelay = 1000000/184;
          break;
        case MPU6050_BAND_94_HZ:
          DBG_PRINT("94");
          sensorReadDelay = 1000000/94;
          break;
        case MPU6050_BAND_44_HZ:
          DBG_PRINT("44");
          sensorReadDelay = 1000000/44;
          break;
        case MPU6050_BAND_21_HZ:
          DBG_PRINT("21");
          sensorReadDelay = 1000000/21;
          break;
        case MPU6050_BAND_10_HZ:
          DBG_PRINT("10");
          sensorReadDelay = 1000000/10;
          break;
        case MPU6050_BAND_5_HZ:
          DBG_PRINT("5");
          sensorReadDelay = 1000000/5;
          break;
        default:
          DBG_PRINTLN("??");
          break;
      }
      DBG_PRINTLN("Hz");

    #elif defined(ADXL343)
      imuSensor.setRange( ADXL343_RANGE_2_G);
      DBG_PRINT( "Accelerometer range set to: ");
      switch ( imuSensor.getRange()) {
        case ADXL343_RANGE_2_G:
          DBG_PRINT("±2");
          break;
        case ADXL343_RANGE_4_G:
          DBG_PRINT("±4");
          break;
        case ADXL343_RANGE_8_G:
          DBG_PRINT("±8");
          break;
        case ADXL343_RANGE_16_G:
          DBG_PRINT("±16");
          break;
        default:
          DBG_PRINT("??");
          break;
      }
      DBG_PRINTLN("G");
      
      imuSensor.setDataRate( ADXL343_DATARATE_25_HZ);
      DBG_PRINT( "Data rate set to: ");
      // confirm data rate and sync polling delay
      switch(imuSensor.getDataRate()) {
        case ADXL343_DATARATE_3200_HZ:
          DBG_PRINT("3200");
          sensorReadDelay = 1000000/3200;
          break;
        case ADXL343_DATARATE_1600_HZ:
          DBG_PRINT("1600");
          sensorReadDelay = 1000000/1600;
          break;
        case ADXL343_DATARATE_800_HZ:
          DBG_PRINT("800");
          sensorReadDelay = 1000000/800;
          break;
        case ADXL343_DATARATE_400_HZ:
          DBG_PRINT("400");
          sensorReadDelay = 1000000/400;
          break;
        case ADXL343_DATARATE_200_HZ:
          DBG_PRINT  ("200");
          sensorReadDelay = 1000000/200;
          break;
        case ADXL343_DATARATE_100_HZ:
          DBG_PRINT("100");
          sensorReadDelay = 1000000/100;
          break;
        case ADXL343_DATARATE_50_HZ:
          DBG_PRINT("50");
          sensorReadDelay = 1000000/50;
          break;
        case ADXL343_DATARATE_25_HZ:
          DBG_PRINT("25");
          sensorReadDelay = 1000000/25;
          break;
        case ADXL343_DATARATE_12_5_HZ:
          DBG_PRINT("12.5");
          sensorReadDelay = 1000000/12.5;
          break;
        case ADXL343_DATARATE_6_25HZ:
          DBG_PRINT("6.25");
          sensorReadDelay = 1000000/6.25;
          break;
        case ADXL343_DATARATE_3_13_HZ:
          DBG_PRINT("3.13");
          sensorReadDelay = 1000000/3.13;
          break;
        case ADXL343_DATARATE_1_56_HZ:
          DBG_PRINT("1.56");
          sensorReadDelay = 1000000/1.56;
          break;
        case ADXL343_DATARATE_0_78_HZ:
          DBG_PRINT("0.78");
          sensorReadDelay = 1000000/0.78;
          break;
        case ADXL343_DATARATE_0_39_HZ:
          DBG_PRINT("0.39");
          sensorReadDelay = 1000000/0.39;
          break;
        case ADXL343_DATARATE_0_20_HZ:
          DBG_PRINT("0.20");
          sensorReadDelay = 1000000/0.2;
          break;
        case ADXL343_DATARATE_0_10_HZ:
          DBG_PRINT("0.10");
          sensorReadDelay = 1000000/0.1;
          break;
        default:
          DBG_PRINT("??");
          break;
      }
      DBG_PRINTLN("Hz");
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
  imuData["fraction"] = String(blockFraction, 3);
  imuData["wheelbase"] = String(wheelbase, 1);
  imuData["track"] = String(track, 1);
  imuData["vanType"] = vanType;

  // Block height under each wheel relative to the lowest wheel (which reads 0)
  float pitchHeight = tanf(fabsf(finalPitch) * DEG_TO_RAD) * wheelbase;
  float rollHeight = tanf(fabsf(finalRoll) * DEG_TO_RAD) * track;
  float lf, rf, lr, rr;
  const char* lowWheel;
  if (finalPitch < 0 && finalRoll < 0) {
    lf = 0; rf = rollHeight; lr = pitchHeight; rr = rollHeight + pitchHeight;
    lowWheel = "lf";
  } else if (finalPitch < 0) {
    lf = rollHeight; rf = 0; lr = rollHeight + pitchHeight; rr = pitchHeight;
    lowWheel = "rf";
  } else if (finalRoll < 0) {
    lf = pitchHeight; rf = rollHeight + pitchHeight; lr = 0; rr = rollHeight;
    lowWheel = "lr";
  } else {
    lf = rollHeight + pitchHeight; rf = pitchHeight; lr = rollHeight; rr = 0;
    lowWheel = "rr";
  }
  
  // round to the nearest block fraction (blockFraction = blocks per inch)
  auto roundBlock = [](float h) { return roundf(h * blockFraction) / blockFraction; };
  imuData["lf"] = String(roundBlock(lf), 2);
  imuData["rf"] = String(roundBlock(rf), 2);
  imuData["lr"] = String(roundBlock(lr), 2);
  imuData["rr"] = String(roundBlock(rr), 2);
  imuData["low"] = lowWheel;

  bool isLevel = (fabsf(finalPitch) <= pTolerance) && (fabsf(finalRoll) <= rTolerance);
  imuData["lvl"] = isLevel;
  
  DBG_PRINT("Filtered Pitch: ");
  DBG_PRINT(finalPitch);
  DBG_PRINT(", Filtered Roll: ");
  DBG_PRINT(finalRoll);
  DBG_PRINTLN("");

  const JSONVar espNowData = getEspNowReadings();
  imuData["espNow"] = espNowData;
  String jsonString = JSON.stringify(imuData);
  return jsonString;
}

// Build the readings JSON and store it for /readings; returns the same JSON for the event stream
String publishReadings() {
  String json = getSensorReadings();
  xSemaphoreTake(readingsMutex, portMAX_DELAY);
  latestReadings = json;
  xSemaphoreGive(readingsMutex);
  return json;
}

// True only if the whole string is a finite number
bool parseStrictFloat(const String &s, float &out) {
  String t = s;
  t.trim();
  if (t.length() == 0) return false;
  char *end;
  float v = strtof(t.c_str(), &end);
  if (*end != '\0' || !isfinite(v)) return false;
  out = v;
  return true;
}

// Reads a request parameter without dereferencing a missing one
bool getParamStr(AsyncWebServerRequest *request, const char *name, bool post, String &out) {
  if (!request->hasParam(name, post)) return false;
  out = request->getParam(name, post)->value();
  return true;
}

// Setup and configuration endpoints use the AP credentials; /readings and / stay open
#define REQUIRE_AUTH(req) if (!(req)->authenticate(esid.c_str(), epass.c_str())) return (req)->requestAuthentication()

bool hasValidSavedWifiCredentials() {
  return (wsid.length() > 0 && wsid != "Not Given" && wpass.length() >= 8);
}

void startStaConnect() {
  WiFi.begin(wsid.c_str(), wpass.c_str());
  lastStaRetryAttempt = millis();
  DBG_PRINTF("Attempting STA connection to %s\n", wsid.c_str());
}

// Called from loop(): while the AP keeps running, keep retrying the saved network
void retryWifiStaConnection() {
  if (!hasValidSavedWifiCredentials() || WiFi.status() == WL_CONNECTED) return;
  // a connection attempt would abort a running scan
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;

  if ((millis() - lastStaRetryAttempt) >= staRetryInterval) {
    setWifiStatusText("AP only - searching for saved WiFi");
    startStaConnect();
  }
}

// Load the saved WiFi credentials and addressing, then start connecting; used at boot and after the setup page saves
void applyWifiSettings() {
  WiFi.disconnect(false);

  wsid = preferences.getString("WIFI_SSID", "");
  DBG_PRINT("\nWifi SSID: ");
  DBG_PRINTLN(wsid);

  wpass = preferences.getString("WIFI_PASS", "");
  DBG_PRINT("Wifi PASSWORD: ");
  #ifdef HIDE_PASSWORDS
    DBG_PRINTLN( "*Hidden*");
  #else
    DBG_PRINTLN(wpass);
  #endif

  String wmode = preferences.getString("WIFI_MODE", "");
  DBG_PRINT("Wifi MODE: ");
  DBG_PRINTLN(wmode);

  if (wsid.length() == 0) {
    wsid = "Not Given";
    localIP = "network not set";
  }

  if (wmode == "static") {
    DBG_PRINTLN("Configuring Static IP settings...");
    String static_ip = preferences.getString("WIFI_IP", "");
    String sub_net = preferences.getString("WIFI_MASK", "");
    String g_add = preferences.getString("WIFI_GATE", "");
    String p_dns = preferences.getString("WIFI_PDNS", "");
    String s_dns = preferences.getString("WIFI_SDNS", "");
    DBG_PRINTF("static_ip: %s sub_net: %s gateway: %s pri-dns: %s sec-dns: %s\n",
               static_ip.c_str(), sub_net.c_str(), g_add.c_str(), p_dns.c_str(), s_dns.c_str());

    IPAddress s_ip, gateway, subnet, primaryDNS, secondaryDNS;
    s_ip.fromString(static_ip);
    gateway.fromString(g_add);
    subnet.fromString(sub_net);
    primaryDNS.fromString(p_dns);
    secondaryDNS.fromString(s_dns);

    if (!WiFi.config(s_ip, gateway, subnet, primaryDNS, secondaryDNS)) {
      DBG_PRINTLN("STA Failed to configure static IP");
    }
  } else {
    DBG_PRINTLN("DHCP WiFi mode configured");
    // all-zero addresses switch the station back to DHCP
    WiFi.config(IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0), IPAddress(0, 0, 0, 0));
  }

  if (hasValidSavedWifiCredentials()) {
    DBG_PRINTLN("Saved WiFi credentials detected. Connecting in AP+STA mode.");
    setWifiStatusText("Connecting to saved WiFi");
    startStaConnect();
  } else {
    DBG_PRINTLN("No valid saved WiFi credentials. AP mode only until configured.");
    setWifiStatusText("AP only - no WiFi configured");
  }
}

// Function to watch for connection events
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  // We only want one SoftAP connection. For basic security, we rely
  // on the AP password to limit who can change our configuration.
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      apConnected = false;
      DBG_PRINTLN("SoftAP client disconnected");
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      apConnected = true;
      DBG_PRINTLN("SoftAP client connected");
      break;
      
    // grab our assigned IP when connecting to a network
    case ARDUINO_EVENT_WIFI_STA_START:
      DBG_PRINTLN("Wi-Fi Station started. Connecting...");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      DBG_PRINT("Successfully connected to router! IP: ");
      localIP = WiFi.localIP().toString();
      setWifiStatusText("Connected to saved WiFi");
      Serial.print("Connected to saved WiFi: ");
      Serial.println(localIP);
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      DBG_PRINTLN("Lost connection to router. Background auto-reconnect active...");
      localIP = "Not Connected";
      if (hasValidSavedWifiCredentials()) {
        setWifiStatusText("AP only - searching for saved WiFi");
      }
      break;
    default:
      break;
  }
}
  
//####################################  BEGIN SETUP  ##################################
void setup() {
  Serial.begin(115200);
  readingsMutex = xSemaphoreCreateMutex();
  initSensor();

  // connect to EEPROM storage
  if (!preferences.begin("config", false)) {
    Serial.println("Failed to initialize Preferences: Halting");
    while (1);
  }
  DBG_PRINTLN("Preferences initialized");

  // mount our file server for our static data and web pages
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS: Halting");
    while (1);
  }
  DBG_PRINTLN("LittleFS successfully mounted");

  // get default SoftAP configuration from file
  File dataFile = LittleFS.open(s_configFile, FILE_READ); //Open File for reading
  if (!dataFile) {
    Serial.println("Config file open failed on read: Halting");
    while (1);
  } else {
    String configContents;
    DBG_PRINTLN("Reading configuration data from file:");
    if (dataFile.available()) configContents = dataFile.readString();
    dataFile.close();
    #ifdef HIDE_PASSWORDS
      DBG_PRINTLN( "*Hidden*");
    #else
      DBG_PRINTLN(configContents);
    #endif

    configJSON = JSON.parse(configContents);
    if (JSON.typeof(configJSON) == "undefined") {
      Serial.println("Parsing configuration file data failed: Halting");
      while (1);
    }
  }

  // reset board to defaults if necessary (i.e. lost AP password)
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

  // retrieve EEPROM sensor calibration values, use declaration values as defauts.
  pOffset = preferences.getFloat("pOffset", pOffset);
  rOffset = preferences.getFloat("rOffset", rOffset);
  pInvert = preferences.getInt("pInvert", pInvert);
  rInvert = preferences.getInt("rInvert", rInvert);
  alpha = preferences.getFloat("alpha", alpha);
  blockFraction = preferences.getFloat("fraction", blockFraction);
  wheelbase = preferences.getFloat("wheelbase", wheelbase);
  track = preferences.getFloat("track", track);
  vanType = preferences.getString("vanType", vanType);
  pTolerance = preferences.getFloat("pTolerance", pTolerance);
  rTolerance = preferences.getFloat("rTolerance", rTolerance);
  
  DBG_PRINT("Retrieved Calibration values: pOffset=");
  DBG_PRINT(pOffset);
  DBG_PRINT(" pInvert=");
  DBG_PRINT(pInvert);
  DBG_PRINT(" rOffset=");
  DBG_PRINT(rOffset);
  DBG_PRINT(" rInvert=");
  DBG_PRINT(rInvert);
  DBG_PRINT(" alpha=");
  DBG_PRINT(alpha);
  DBG_PRINT(" fraction=");
  DBG_PRINT(blockFraction);
  DBG_PRINT(" pTolerance=");
  DBG_PRINT(pTolerance);
  DBG_PRINT(" rTolerance=");
  DBG_PRINTLN(rTolerance);

  // retrieve EEPROM AP configuration
  esid = preferences.getString("AP_SSID", "");
  epass = preferences.getString("AP_PASS", "");
  String eap = preferences.getString("AP_IP", "");

  // if EEPROM info is missing, use defaults from config file
  if ((esid == "") && (epass == "")) {
    DBG_PRINTLN("\nInitial SoftAP user/pass configuration");
    esid = String(configJSON["AP_name"]);
    epass = String(configJSON["AP_pass"]);
    preferences.putString("AP_SSID", esid);
    preferences.putString("AP_PASS", epass);
  }
  if (eap == "") {
    DBG_PRINTLN("\nInitial SoftAP address configuration");
    eap = String(configJSON["AP_IP"]);
    preferences.putString("AP_IP", eap);
  }

  DBG_PRINT("Access point SSID: ");
  DBG_PRINTLN(esid);
  DBG_PRINT("Access point PASSWORD: ");
  #ifdef HIDE_PASSWORDS
    DBG_PRINTLN( "*Hidden*");
  #else
    DBG_PRINTLN(epass);
  #endif
  DBG_PRINT("Access point ADDRESS: ");
  DBG_PRINTLN(eap);

  // start our SoftAP with one allowed connection
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPsetHostname(esid.c_str());
  if (!WiFi.softAP(esid.c_str(), epass.c_str(), 1, 0, 1))
  {
    Serial.println("SoftAP creation failed: Halting");
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

  // HTML+Images server callbacks
  // handlers are evaluated in the order they are attached to the server.
  server.onNotFound([](AsyncWebServerRequest * request) {
    request -> send(404, "text/html", "<h1 align=\"center\">Oops! It's not here.</h1>");
  });

  server.on("/", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    // our van level image page with blocking stack heights
    request -> send(LittleFS, "/index.html", "text/html");
  });

  server.on("/setup", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    if(!request->authenticate(esid.c_str(), epass.c_str()))
      return request->requestAuthentication();
    request -> send(LittleFS, "/setup.html", "text/html");
  });

  server.on("/setup.html", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    if(!request->authenticate(esid.c_str(), epass.c_str()))
      return request->requestAuthentication();
    request -> send(LittleFS, "/setup.html", "text/html");
  });

  // server callback for RESTful IMU update readings
  server.on( "/readings", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest *request){
    xSemaphoreTake(readingsMutex, portMAX_DELAY);
    String json = latestReadings;
    xSemaphoreGive(readingsMutex);
    request->send( 200, "application/json", json);
  });

  // server callback button handlers
  server.on("/setupData", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    JSONVar data;
    data["default_pass"] = (epass == String(configJSON["AP_pass"]));
    data["ver"] = String(VERSION);
    data["myIP"] = myIP;
    data["localIP"] = localIP;
    data["wsid"] = wsid;
    data["apname"] = esid;
    data["p_offset"] = String(pOffset);
    data["p_invert"] = String(pInvert);
    data["r_offset"] = String(rOffset);
    data["r_invert"] = String(rInvert);
    data["alpha"] = String(alpha, 3);
    data["fraction"] = String(blockFraction, 3);
    data["wheelbase"] = String(wheelbase, 1);
    data["track"] = String(track, 1);
    data["van_type"] = vanType;
    data["p_tolerance"] = String(pTolerance, 2);
    data["r_tolerance"] = String(rTolerance, 2);
    data["wifi_state"] = wifiStatusText;
    data["wifi_channel"] = WiFi.channel();
    data["MAC"] = WiFi.macAddress();
    request -> send(200, "application/json", JSON.stringify(data));
  });

  // Starts an async scan on the first call; the page polls until it gets the result array
  server.on("/scanWifi", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      DBG_PRINTLN("Scanning Wifi");
      WiFi.scanNetworks(true);
      request -> send(202, "application/json", "{\"scanning\":true}");
      return;
    }
    if (n == WIFI_SCAN_RUNNING) {
      request -> send(202, "application/json", "{\"scanning\":true}");
      return;
    }
    if (n == 0) {
      WiFi.scanDelete();
      request -> send(200, "application/json", "[]");
      return;
    }
    JSONVar list;
    for (int i = 0; i < n; ++i) {
      JSONVar net;
      net["rssi"] = WiFi.RSSI(i);
      net["ssid"] = WiFi.SSID(i);
      net["bssid"] = WiFi.BSSIDstr(i);
      net["channel"] = WiFi.channel(i);
      net["secure"] = (int)WiFi.encryptionType(i);
      list[i] = net;
    }
    WiFi.scanDelete();
    request -> send(200, "application/json", JSON.stringify(list));
  });

  server.on("/applyBtnFunction", WebRequestMethod::HTTP_POST, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    DBG_PRINTLN( "Assigning AP");
    String txtssid, txtpass, txtaplan;
    if (!getParamStr(request, "txtssid", true, txtssid) || !getParamStr(request, "txtpass", true, txtpass)
        || !getParamStr(request, "txtaplan", true, txtaplan)) {
      request -> send(400, "text/plain", "missing parameter");
      return;
    }
    if (txtssid.length() > 0) {
      preferences.putString("AP_SSID", txtssid);
      DBG_PRINT("Writing AP SSID :: ");
      DBG_PRINTLN(txtssid);
    }
    // WPA2 passwords must be 8 or more characters. SoftAP will fail if fewer.
    if (txtpass.length() > 7) {
      preferences.putString("AP_PASS", txtpass);
      DBG_PRINT("Writing AP PASSWORD :: ");
      #ifdef HIDE_PASSWORDS
        DBG_PRINTLN( "*Hidden*");
      #else
        DBG_PRINTLN(txtpass);
      #endif
    } else {
      DBG_PRINT("New AP Password, \"");
      DBG_PRINT(txtpass);
      DBG_PRINTLN("\", too short, not saved");
    }
    if (txtaplan.length() > 0) {
      preferences.putString("AP_IP", txtaplan);
      DBG_PRINT("Writing AP IP :: ");
      DBG_PRINTLN(txtaplan);
    }
    request -> send(200, "text/plain", "ok");
  });

  server.on("/connectBtnFunction", WebRequestMethod::HTTP_POST, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    DBG_PRINTLN( "Assigning Wifi");
    String wifi_ssid, wifi_pass, wifi_mode;
    if (!getParamStr(request, "wifi_ssid", true, wifi_ssid) || !getParamStr(request, "wifi_pass", true, wifi_pass)
        || !getParamStr(request, "wifi_mode", true, wifi_mode)) {
      request -> send(400, "text/plain", "missing parameter");
      return;
    }
    DBG_PRINTLN(wifi_ssid);
    #ifdef HIDE_PASSWORDS
      DBG_PRINTLN( "*Hidden*");
    #else
      DBG_PRINTLN(wifi_pass);
    #endif
    DBG_PRINTLN(wifi_mode);
    if (wifi_ssid.length() > 0) {
      preferences.putString("WIFI_SSID", wifi_ssid);
      DBG_PRINT("Writing WIFI SSID :: ");
      DBG_PRINTLN(wifi_ssid);
    }
    if (wifi_pass.length() > 7) { // WPA2 requires a least 8 chars
      preferences.putString("WIFI_PASS", wifi_pass);
      DBG_PRINT("Writing WIFI PASSWORD :: ");
      #ifdef HIDE_PASSWORDS
        DBG_PRINTLN( "*Hidden*");
      #else
        DBG_PRINTLN(wifi_pass);
      #endif
    } else {
      DBG_PRINT("New WiFi Password, \"");
      DBG_PRINT(wifi_pass);
      DBG_PRINTLN("\", too short, not saved");
    }

    // writing WIFI settings to EEPROM
    preferences.putString("WIFI_MODE", wifi_mode);
    DBG_PRINT("Writing WIFI MODE :: ");
    DBG_PRINTLN(wifi_mode);

    if (wifi_mode == "static") {
      // the page posts these in the request body
      String txtipadd, net_m, g_add, p_dns, s_dns;
      if (!getParamStr(request, "txtipadd", true, txtipadd) || !getParamStr(request, "net_m", true, net_m)
          || !getParamStr(request, "g_add", true, g_add) || !getParamStr(request, "p_dns", true, p_dns)) {
        request -> send(400, "text/plain", "missing parameter");
        return;
      }
      getParamStr(request, "s_dns", true, s_dns);

      if (txtipadd.length() > 0) {
        preferences.putString("WIFI_IP", txtipadd);
        DBG_PRINT("Writing WIFI IP :: ");
        DBG_PRINTLN(txtipadd);
      }
      if (net_m.length() > 0) {
        preferences.putString("WIFI_MASK", net_m);
        DBG_PRINT("Writing WIFI MASK :: ");
        DBG_PRINTLN(net_m);
      }
      if (g_add.length() > 0) {
        preferences.putString("WIFI_GATE", g_add);
        DBG_PRINT("Writing WIFI GATEWAY :: ");
        DBG_PRINTLN(g_add);
      }
      if (p_dns.length() > 0) {
        preferences.putString("WIFI_PDNS", p_dns);
        DBG_PRINT("Writing WIFI PDNS :: ");
        DBG_PRINTLN(p_dns);
      }
      if (s_dns.length() > 0) {
        preferences.putString("WIFI_SDNS", s_dns);
        DBG_PRINT("Writing WIFI SDNS :: ");
        DBG_PRINTLN(s_dns);
      }
    }
    wifiApplyPending = true; // loop() applies the new settings; no reboot needed
    request -> send(200, "text/plain", "ok");
  });

  server.on("/applyOffsetFunction", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    DBG_PRINTLN( "Assigning Offsets");
    String sPOff, sROff, sAlpha, sFraction, sPTol, sRTol, sPInv, sRInv;
    if (!getParamStr(request, "p_offset", false, sPOff) || !getParamStr(request, "r_offset", false, sROff)
        || !getParamStr(request, "alpha", false, sAlpha) || !getParamStr(request, "fraction", false, sFraction)
        || !getParamStr(request, "p_tolerance", false, sPTol) || !getParamStr(request, "r_tolerance", false, sRTol)
        || !getParamStr(request, "p_invert", false, sPInv) || !getParamStr(request, "r_invert", false, sRInv)) {
      request -> send(400, "text/plain", "missing parameter");
      return;
    }

    // validate everything before changing any live value
    float newAlpha, newFraction, newPTol, newRTol;
    if (!parseStrictFloat(sAlpha, newAlpha) || newAlpha <= 0.0f || newAlpha >= 1.0f) {
      request -> send(400, "text/plain", "Filter alpha must be a number greater than 0 and less than 1.");
      return;
    }
    if (!parseStrictFloat(sFraction, newFraction) || newFraction <= 0.0f) {
      request -> send(400, "text/plain", "Block size must be a number greater than 0.");
      return;
    }
    if (!parseStrictFloat(sPTol, newPTol) || !parseStrictFloat(sRTol, newRTol) || newPTol < 0.0f || newRTol < 0.0f) {
      request -> send(400, "text/plain", "Tolerances must be numbers of 0 or more.");
      return;
    }

    // if unable to parse the offset values, the variables will contain 0 which is okay
    pOffset = sPOff.toFloat();
    rOffset = sROff.toFloat();
    alpha = newAlpha;
    blockFraction = newFraction;
    pTolerance = newPTol;
    rTolerance = newRTol;
    
    // the invert variables will be 0 on failure and those need to be change to 1
    pInvert = sPInv.toInt();
    rInvert = sRInv.toInt();

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
    preferences.putFloat("alpha", alpha);
    preferences.putFloat("fraction", blockFraction);
    preferences.putFloat("pTolerance", pTolerance);
    preferences.putFloat("rTolerance", rTolerance);
    
    DBG_PRINT("Calibration values set: pOffset=");
    DBG_PRINT(pOffset);
    DBG_PRINT(" pInvert=");
    DBG_PRINT(pInvert);
    DBG_PRINT(" rOffset=");
    DBG_PRINT(rOffset);
    DBG_PRINT(" rInvert=");
    DBG_PRINT(rInvert);
    DBG_PRINT(" alpha=");
    DBG_PRINT(alpha);
    DBG_PRINT(" fraction=");
    DBG_PRINT(blockFraction);
    DBG_PRINT(" pTolerance=");
    DBG_PRINT(pTolerance);
    DBG_PRINT(" rTolerance=");
    DBG_PRINTLN(rTolerance);
    
    request -> send(200, "text/plain", "ok");
  });

  server.on("/applyVehicleFunction", WebRequestMethod::HTTP_GET, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    if (request->hasParam("van_type")) {
      String t = request->getParam("van_type")->value();
      // quotes and backslashes would break the hand-built setupData JSON
      t.replace("\"", "");
      t.replace("\\", "");
      t.trim();
      if (t.length() > 32) t = t.substring(0, 32);
      if (t.length() > 0) vanType = t;
    }
    if (request->hasParam("wheelbase")) {
      float v = request->getParam("wheelbase")->value().toFloat();
      if (v >= 100 && v <= 200) wheelbase = v;
    }
    if (request->hasParam("track")) {
      float v = request->getParam("track")->value().toFloat();
      if (v >= 50 && v <= 100) track = v;
    }
    preferences.putString("vanType", vanType);
    preferences.putFloat("wheelbase", wheelbase);
    preferences.putFloat("track", track);
    request -> send(200, "text/plain", "ok");
  });

  server.on("/rebootBtnFunction", WebRequestMethod::HTTP_POST, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    DBG_PRINTLN( "Requesting Restart");
    if (request->hasParam("reboot_btn", true) && request -> getParam("reboot_btn", true) -> value() == "reboot_device") {
      DBG_PRINTLN("restarting device");
      request -> send(200, "text/plain", "ok");
      // handle rebooting in the loop() - watchdog gets upset if we block with Delay() here.
      bootStart = millis();
    } else {
      request -> send(400, "text/plain", "bad request");
    }
  });

  server.on("/resetBtnFunction", WebRequestMethod::HTTP_POST, [](AsyncWebServerRequest * request) {
    REQUIRE_AUTH(request);
    DBG_PRINTLN( "Requesting Reset");
    if (request->hasParam("reset_btn", true) && request -> getParam("reset_btn", true) -> value() == "reset_device") {
      preferences.clear();
      DBG_PRINTLN("EEPROM cleared");
      request -> send(200, "text/plain", "ok");
    } else {
      request -> send(400, "text/plain", "bad request");
    }
  });

  // only these folders are public; APconfig.json and the HTML pages are not served from here
  server.serveStatic("/images", LittleFS, "/images");
  
  // server event callback
  events.onConnect([](AsyncEventSourceClient * client) {
    if (client -> lastId()) {
      DBG_PRINTF("Client reconnected! Last message ID received: %lu\n", client -> lastId());
    }
    // send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client -> send("hello!", NULL, millis(), 1000);
  });
  server.addHandler( & events);

  // setup our Wifi connection
  WiFi.setAutoReconnect(true); // Tells the ESP32 internal core to reconnect automatically
  applyWifiSettings();
  WiFi.setSleep(false);
  initEspNow();

  publishReadings(); // so /readings has valid data before the first loop() publish
  server.begin(); // start the webserver for the Access Point and Station
  
  // start the OTA service using our SoftAP credentials
  ElegantOTA.begin(&server, esid.c_str(), epass.c_str());

  Serial.println("Startup complete");
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
  
  // keep retrying the saved WiFi while the AP stays available
  if (wifiApplyPending) {
    wifiApplyPending = false;
    applyWifiSettings();
  }
  retryWifiStaConnection();
  
  // poll IMU at the sensor bandwidth rate
  if ((micros() - lastSensorReadTime) >= sensorReadDelay) {
    updateSensorData();
    lastSensorReadTime = micros();
  }
  
  // publish readings at the same rate for both /readings and the event stream
  if ((millis() - lastWebTime) >= webDelay) {
    lastWebTime = millis();
    String json = publishReadings();
    if (events.count() > 0) {
      events.send(json.c_str(), "new_readings", millis());
    }
  }
}
