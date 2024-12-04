#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

#define SERIAL_DEBUG = true;

#define STASSID "Jan-network"
#define STAPSK "K54fXzuu"
#define SVR_PORT 80

#define WIFI_CHECK_INTERVAL 1000L
#define MAX_WIND_VALUES 200

const char* ssid = STASSID;
const char* password = STAPSK;

const char* www_username = "axiom";
const char* www_password = "MM6ZJhkGUxrr";

Adafruit_BMP280 bmp; 
ESP8266WebServer webServer(SVR_PORT);

int BMP280;

float windSpeed[MAX_WIND_VALUES];
unsigned int iterationNo = 0;

struct measuredData{
  float temp = 0.0;
  float pressure = 0.0;
  float windSpeed = 0;
  float windGust = 0;
};
measuredData measure;

struct settingsData{
  float anemometrK = 15.3925;
  int tempMesaureInterval = 60;
  int pressureMesaureInterval = 60;
};
settingsData settings;

void readSettings() {
  EEPROM.begin(4);
  uint16_t settingsSize = 0;
  EEPROM.get(0, settingsSize);
  EEPROM.end();

  // read only if saved structure has the same size as actual
  if(settingsSize == sizeof(settings)) {
    EEPROM.begin(sizeof(settings));
    EEPROM.get(0, settings);
    EEPROM.end();
  }
}

void saveSettings() {
  EEPROM.begin(sizeof(uint16_t));
  uint16_t size = sizeof(settings);
  EEPROM.put(0, size);
  EEPROM.commit();
  EEPROM.end();

  EEPROM.begin(sizeof(settings));
  EEPROM.put(0, settings);
  EEPROM.commit();
  EEPROM.end();
}

void measureTP() {
  if(BMP280) {
    if(iterationNo % settings.tempMesaureInterval == 0) measure.temp = bmp.readTemperature();
    if(iterationNo % settings.pressureMesaureInterval == 0) measure.pressure = bmp.readPressure() * 0.007500615;
  }
}

void measureWindSpeed() {

  windSpeed[iterationNo % MAX_WIND_VALUES] = float(analogRead(0)) / settings.anemometrK;

  float maxVal = 0;
  float avgVal = 0;
  int pointCnt = 0;

  for(byte i = 0; i < MAX_WIND_VALUES; i++) {
    if(windSpeed[i] > maxVal) maxVal = windSpeed[i];
    if(i < iterationNo) {
      avgVal += windSpeed[i];
      pointCnt++;
    }
  }

  measure.windGust = maxVal;
  if(pointCnt > 0) measure.windSpeed = avgVal / pointCnt;
}

void connectWiFi() {

  webServer.stop();

  int status = WL_IDLE_STATUS;

  int retryCnt = 20;  
  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, password);
    #ifdef SERIAL_DEBUG
      Serial.print(".");
    #endif
    delay(300);
    if(retryCnt-- < 0) break;
  }

  if(status == WL_CONNECTED) webServer.begin();

  #ifdef SERIAL_DEBUG
    Serial.print(" IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();
  #endif
}

void check_WiFi() {

  static uint32_t checkwifi_timeout = 0;
  static uint32_t current_millis;

  current_millis = millis();

  if ((current_millis > checkwifi_timeout) || (checkwifi_timeout == 0)) {
    
    if (WiFi.status() != WL_CONNECTED) {
      #ifdef SERIAL_DEBUG
        Serial.println(F("\nWiFi lost, trying reconnect"));
      #endif
      connectWiFi();
    }

    checkwifi_timeout = current_millis + WIFI_CHECK_INTERVAL;
  }

}

void handleRoot() {

  if (!webServer.authenticate(www_username, www_password)) {
    return webServer.requestAuthentication();
  }
  
  JsonDocument json;

  json["temp"] = measure.temp;
  json["pressure"] = measure.pressure;
  json["wind"] = measure.windSpeed;
  json["gusts"] = measure.windGust;

  String outStr;

  serializeJson(json, outStr);

  webServer.send(
    200,
    "application/json",
    outStr
  );
}

void handleNotFound() {
  webServer.send(
    404,
    "text/plain",
    "Not found"
  );
}

void setup() {

  webServer.on("/", handleRoot);    
  webServer.onNotFound(handleNotFound);  

  #ifdef SERIAL_DEBUG
    Serial.begin(115200);
    while ( !Serial ) delay(100);   // wait for native usb

    Serial.println(F("Init BMP280"));
  #endif

  connectWiFi();

  BMP280 = bmp.begin(0x76);
  if (BMP280) {
    /* Default settings from datasheet. */
    bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                    Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                    Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                    Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                    Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
    #ifdef SERIAL_DEBUG
      Serial.println(F("BMP280 initialized"));
    #endif
  }
}

void loop() {

  check_WiFi();

  measureTP();
  measureWindSpeed();

  #ifdef SERIAL_DEBUG
    Serial.println(iterationNo);
    Serial.print(F("Wind speed, m/s: ")); Serial.println(measure.windSpeed);
    Serial.print(F("Wind gust, m/s: ")); Serial.println(measure.windGust);
    Serial.print(F("Temperature, °C: ")); Serial.println(measure.temp);
    Serial.print(F("Pressure, mmHg: ")); Serial.println(measure.pressure);
    Serial.println();
  #endif

  int waitStart = millis();
  while(millis() - waitStart < 1000) {
    webServer.handleClient();
  }
  
  iterationNo++;
  if(iterationNo > 65530) iterationNo = 0;
}
