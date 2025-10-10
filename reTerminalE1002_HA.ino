/**
 * @file reTerminalE1002_HA.ino
 * @brief Weather, Home Assistant, and Bitcoin dashboard on Seeed reTerminal E1002 e-paper.
 * 
 * - Fetches weather data from Open-Meteo API.
 * - Fetches Home Assistant sensor temperatures via REST API.
 * - Fetches Bitcoin price in USD from CoinGecko.
 * - Displays all data on reTerminal E1002 7.3" color e-paper.
 * - Handles deep sleep and wake-up via button or timer.
 *   https://tutoduino.fr/
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <GxEPD2_7C.h>

#include <FreeSans12pt7b_mod.h>
#include <FreeSans18pt7b_mod.h>
#include <FreeSans24pt7b_mod.h>
#include <Fonts/FreeSans9pt7b.h>

#include <Wire.h>
#include <SensirionI2cSht4x.h>
#include "icons_50x50.h"
#include "icons_100x100.h"
#include "icons.h"
#include "secrets.h"

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */

// Battery monitoring pins
#define BATTERY_ADC_PIN 1      // GPIO1 - Battery voltage ADC
#define BATTERY_ENABLE_PIN 21  // GPIO21 - Battery monitoring enable

// Serial communication pins for debugging
#define SERIAL_RX 44
#define SERIAL_TX 43

// SPI pinout for ePaper display (verify for your hardware)
#define EPD_SCK_PIN 7
#define EPD_MOSI_PIN 9
#define EPD_CS_PIN 10
#define EPD_DC_PIN 11
#define EPD_RES_PIN 12
#define EPD_BUSY_PIN 13
#define GREEN_BUTTON 3  // Deep sleep wake-up button

// I2C pins for reTerminal E Series
#define I2C_SDA 19
#define I2C_SCL 20

// Select the ePaper driver to use
// 0: reTerminal E1001 (7.5'' B&W)
// 1: reTerminal E1002 (7.3'' Color)
#define EPD_SELECT 1

#if (EPD_SELECT == 0)
#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
#define GxEPD2_DRIVER_CLASS GxEPD2_750_GDEY075T7
#elif (EPD_SELECT == 1)
#define GxEPD2_DISPLAY_CLASS GxEPD2_7C
#define GxEPD2_DRIVER_CLASS GxEPD2_730c_GDEP073E01
#endif

#define MAX_DISPLAY_BUFFER_SIZE 16000

#define MAX_HEIGHT(EPD) \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) \
     ? EPD::HEIGHT \
     : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>
  display(GxEPD2_DRIVER_CLASS(/*CS=*/EPD_CS_PIN, /*DC=*/EPD_DC_PIN,
                              /*RST=*/EPD_RES_PIN, /*BUSY=*/EPD_BUSY_PIN));

// Global variable for SPI communication
SPIClass hspi(HSPI);

// French weekday and month names for date formatting on display
const char *jours[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi" };
const char *mois[] = { "Janvier", "Février", "Mars", "Avril", "Mai", "Juin", "Juillet", "Août", "Septembre", "Octobre", "Novembre", "Décembre" };

// Global weather variables
int D0MinTemp, D0MaxTemp, D1MinTemp, D1MaxTemp, D2MinTemp, D2MaxTemp, D3MinTemp, D3MaxTemp, D4MinTemp, D4MaxTemp;
int D0Code, D1Code, D2Code, D3Code, D4Code;
String localTime = "2025-01-01 00:00";
int currentTemp;
int g_x_start, g_y_start;

// Object to manage onboard SHT4x sensor (I2C temperature/humidity)
SensirionI2cSht4x sht4x;
const float sht4xCalibration = -1;  // SHT4x calibration offset

// 3.7 V Li-Ion battery voltage
const float minVoltage = 3.0;
const float maxVoltage = 4.0;

/**
 * @brief Mapp float voltage to % battery charge.
 * @param x=Battery voltage
 * @param in_min=Battery min voltage
 * @param in_max=Battery max voltage
 * @return % of battery charge.
 */
uint8_t mapFloat(float x, float in_min, float in_max) {
  float val;
  val = (x - in_min) * (100) / (in_max - in_min);
  if (val < 0) {
    val = 0;
  } else if (val > 100) {
    val = 100;
  }
  return (uint8_t)val;
}

/**
 * @brief Fetch temperature from Home Assistant REST API.
 * @param entityId - Home Assistant sensor entity (e.g., "sensor.lumi_lumi_weather_temperature")
 * @return Temperature value or 0.0 if error.
 */
float getHomeAssistantSensorState(String entityId) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("ERROR: Wi-Fi not connected.");
    return 0.0;
  }
  HTTPClient http;
  String url = String(ha_url) + entityId;
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + String(ha_token));
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial1.print("HTTP Error: ");
    Serial1.println(httpCode);
    Serial1.println(http.getString());
    http.end();
    return 0.0;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) {
    Serial1.print("JSON parsing error: ");
    Serial1.println(error.c_str());
    return 0.0;
  }
  if (!doc.containsKey("state")) {
    Serial1.println("ERROR: 'state' field missing in JSON response.");
    return 0.0;
  }
  float state = doc["state"].as<float>();
  return state;
}

/**
 * @brief Fetch weather data from Open-Meteo API (current and next 4 days).
 * @return 0 on success, <0 on error.
 */
int fetchWeatherData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("WiFi not connected");
    return -1;
  }
  HTTPClient http;
  http.begin(weather_url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial1.print("HTTP Error: ");
    Serial1.println(httpCode);
    Serial1.println(http.getString());
    http.end();
    return -2;
  }
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) {
    Serial1.print("JSON parsing error: ");
    Serial1.println(error.c_str());
    return -3;
  }
  localTime = doc["current"]["time"].as<String>();
  D0Code = doc["current"]["weather_code"].as<int>();
  currentTemp = doc["current"]["temperature"].as<int>();
  D0MinTemp = doc["daily"]["temperature_2m_min"][0].as<int>();
  D0MaxTemp = doc["daily"]["temperature_2m_max"][0].as<int>();
  D1MinTemp = doc["daily"]["temperature_2m_min"][1].as<int>();
  D1MaxTemp = doc["daily"]["temperature_2m_max"][1].as<int>();
  D1Code = doc["daily"]["weather_code"][1].as<int>();
  D2MinTemp = doc["daily"]["temperature_2m_min"][2].as<int>();
  D2MaxTemp = doc["daily"]["temperature_2m_max"][2].as<int>();
  D2Code = doc["daily"]["weather_code"][2].as<int>();
  D3MinTemp = doc["daily"]["temperature_2m_min"][3].as<int>();
  D3MaxTemp = doc["daily"]["temperature_2m_max"][3].as<int>();
  D3Code = doc["daily"]["weather_code"][3].as<int>();
  D4MinTemp = doc["daily"]["temperature_2m_min"][4].as<int>();
  D4MaxTemp = doc["daily"]["temperature_2m_max"][4].as<int>();
  D4Code = doc["daily"]["weather_code"][4].as<int>();
  return 0;
}

/**
 * @brief Get Bitcoin price (USD) from CoinGecko API.
 * @return Bitcoin price as integer, 0 if not available.
 */
int getBTC() {
  int btc = 0;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(btc_url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(1024);
      if (!deserializeJson(doc, http.getString()))
        btc = doc["bitcoin"]["usd"].as<int>();
    }
    http.end();
  }
  return btc;
}


/**
 * @brief Get Ethereum price (USD) from CoinGecko API.
 * @return Ethereum price as integer, 0 if not available.
 */
int getETH() {
  int eth = 0;
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(eth_url);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      DynamicJsonDocument doc(1024);
      if (!deserializeJson(doc, http.getString()))
        eth = doc["ethereum"]["usd"].as<int>();
    }
    http.end();
  }
  return eth;
}

/**
 * @brief Zeller's congruence to compute day of week (0=Sunday).
 * @param y Year
 * @param m Month
 * @param d Day
 * @return Weekday index
 */
int jourDeLaSemaine(int y, int m, int d) {
  if (m < 3) {
    m += 12;
    y -= 1;
  }
  int K = y % 100;
  int J = y / 100;
  int h = (d + 13 * (m + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
  return ((h + 6) % 7);  // 0=Sunday
}

/**
 * @brief Formats ISO date string as French date for display.
 * @param dateStr "YYYY-MM-DD HH:MM"
 * @return "Samedi 04 Octobre" style string.
 */
String formatDateFR(String dateStr) {
  int y = dateStr.substring(0, 4).toInt();
  int m = dateStr.substring(5, 7).toInt();
  int d = dateStr.substring(8, 10).toInt();
  int wday = jourDeLaSemaine(y, m, d);
  return String(jours[wday]) + " " + (d < 10 ? "0" : "") + String(d) + " " + mois[m - 1];
}

/**
 * @brief Maps Open-Meteo weather code to icon index.
 * @param weatherCode Open-Meteo code
 * @return Icon bitmap index
 */
int weatherCodeToIcon(int weatherCode) {
  switch (weatherCode) {
    case 0: return 7;  // Sun
    case 1:
    case 2: return 3;  // Some clouds
    case 3: return 5;  // Cloudy
    case 45:
    case 48: return 2;  // Fog
    case 51:
    case 53:
    case 55:
    case 56:
    case 57: return 7;  // Freezing drizzle
    case 61:
    case 63:
    case 65:
    case 66:
    case 67: return 6;  // Freezing rain
    case 71:
    case 73:
    case 75:
    case 77: return 4;  // Snow
    case 80:
    case 81:
    case 82: return 8;  // Showers
    case 85:
    case 86: return 4;  // Snow
    case 95:
    case 96:
    case 99: return 0;  // Thunderstorm
    default: return 5;
  }
}

/**
 * @brief Display forecast for one day at (x, y) with weather icon.
 */
void displayForecast(int x, int y, int day, int min, int max, int iconNb) {
  int16_t x1, y1;
  uint16_t w, h;
  display.drawBitmap(x + 30, y + 50, epd_bitmap_allArray[iconNb], 50, 50, GxEPD_BLACK);
  display.setFont(&FreeSans12pt7b);
  display.getTextBounds(jours[day], x, y, &x1, &y1, &w, &h);
  display.setCursor(x + 55 - w / 2, y + 40);
  display.print(jours[day]);
  display.setFont(&FreeSans12pt7b);
  display.getTextBounds(String(max) + "`", x, y, &x1, &y1, &w, &h);
  display.setCursor(x + 55 - w / 2, y + 130);
  display.print(max, 0);
  display.write(0x60);
  display.getTextBounds(String(min) + "`", x, y, &x1, &y1, &w, &h);
  display.setCursor(x + 55 - w / 2, y + 160);
  display.print(min, 0);
  display.write(0x60);
}

/**
 * @brief Displays today's weather with large icon and temperature.
 */
void displayCurrent(int x, int y, float current, int min, int max, int iconNb) {
  int16_t x1, y1;
  uint16_t w, h;
  display.drawBitmap(x + 30, y + 50, epd_bitmap2_allArray[iconNb], 100, 100, GxEPD_BLACK);
  display.setFont(&FreeSans24pt7b);
  display.setCursor(x + 150, y + 130);
  display.print(current, 0);
  display.write(0x60);
  display.setFont(&FreeSans12pt7b);

  display.getTextBounds(String(max) + "`", 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + 80 - w / 2, y + 170);
  display.print(max, 0);
  display.write(0x60);

  display.getTextBounds(String(min) + "`", 0, 0, &x1, &y1, &w, &h);
  display.setCursor(x + 80 - w / 2, y + 200);
  display.print(min, 0);
  display.write(0x60);
}

/**
 * @brief Read actual battery voltage from ADC.
 * @return Battery voltage in Volts.
 */
float getBatteryVoltage() {
  digitalWrite(BATTERY_ENABLE_PIN, HIGH);
  delay(5);
  int mv = analogReadMilliVolts(BATTERY_ADC_PIN);
  digitalWrite(BATTERY_ENABLE_PIN, LOW);
  return ((mv / 1000.0) * 2);  // Correction for voltage divider
}

/**
 * @brief Arduino setup: serial, display, WiFi, sensors.
 */
void setup() {
  Serial1.begin(115200, SERIAL_8N1, SERIAL_RX, SERIAL_TX);
  delay(500);
  pinMode(GREEN_BUTTON, INPUT_PULLUP);

  // Prepare deep sleep wake-up on button press
  esp_sleep_enable_ext0_wakeup((gpio_num_t)GREEN_BUTTON, LOW);

  // SPI and display initialization
  hspi.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, -1);
  display.epd2.selectSPI(hspi, SPISettings(2000000, MSBFIRST, SPI_MODE0));
  display.init(0);
  display.setRotation(0);

  // Connect to WiFi
  WiFi.begin(wifi_ssid, wifi_password);
  Serial1.print("Connecting to WiFi...");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(500);
    Serial1.print(".");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial1.println("[ERROR] WiFi connection failed, restart required.");
    ESP.restart();
  }

  // Initialize I2C with custom pins
  Wire.begin(I2C_SDA, I2C_SCL);

  // Initialize onboard SHT4x sensor
  sht4x.begin(Wire, 0x44);

  // Configure battery monitoring
  pinMode(BATTERY_ENABLE_PIN, OUTPUT);
  digitalWrite(BATTERY_ENABLE_PIN, HIGH);  // Enable battery monitoring

  // Configure ADC for battery
  analogReadResolution(12);  // 12-bit ADC
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);
}

/**
 * @brief Main loop: fetch sensors, display dashboard, go to deep sleep.
 */
void loop() {
  int x, y;
  int x_ha_box, y_ha_box, ha_box_w, ha_box_h;                          // home assistant box
  int x_current_box, y_current_box, current_box_w, current_box_h;      // current weather box
  int x_forecast_box, y_forecast_box, forecast_box_w, forecast_box_h;  // forecast box
  int x_bitcoin_box, y_bitcoin_box, bitcoin_box_w, bitcoin_box_h;      // bitcoin box
  int x_battery_box, y_battery_box, battery_box_w, battery_box_h;      // battery box
  int16_t x1, y1;
  uint16_t w, h;
  esp_err_t esp_error;

  float internalTemperatureSensor, externalTemperatureSensor, greenHouseTemperature;
  float min, max;
  String icon;
  float sht4xTemperature, sht4xHumidity;
  uint8_t vBatPercentage;
  float vBat;

  // Measure reTerminal internal SHT4 sensor
  uint16_t error = sht4x.measureMediumPrecision(sht4xTemperature, sht4xHumidity);
  sht4xTemperature += sht4xCalibration;

  // Measure battery voltage
  vBat = getBatteryVoltage();
  vBatPercentage = mapFloat(vBat, minVoltage, maxVoltage);

  // Get Bitcoind value
  int btc = getBTC();
  int eth = getETH();

  // Fetch weather data for current and next 4 days
  if (fetchWeatherData() != 0) {
    Serial1.print("fetchWeatherData failed!");
    return;
  }

  String dateFR = formatDateFR(localTime);  // French formatted date
  int today = jourDeLaSemaine(localTime.substring(0, 4).toInt(), localTime.substring(5, 7).toInt(), localTime.substring(8, 10).toInt());

  // Fetch Home Assistant sensor states
  internalTemperatureSensor = getHomeAssistantSensorState("sensor.tutoduino_esp32c6tempsensor_temperature");
  externalTemperatureSensor = getHomeAssistantSensorState("sensor.lumi_lumi_weather_temperature");
  greenHouseTemperature = getHomeAssistantSensorState("sensor.lumi_lumi_weather_temperature_2");

  display.setFullWindow();
  display.firstPage();

  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(600, 470);
    display.print(localTime);

    // Display date on top
    display.setFont(&FreeSans18pt7b);
    display.getTextBounds(dateFR, 0, 2, &x1, &y1, &w, &h);
    display.setCursor(400 - w / 2, 40);
    display.print(dateFR);

    // Current weather box
    x_current_box = 30;
    y_current_box = 60;
    current_box_w = 240;
    current_box_h = 210;
    display.fillRect(x_current_box, y_current_box, current_box_w, 40, GxEPD_GREEN);
    display.drawRect(x_current_box, y_current_box, current_box_w, current_box_h, GxEPD_BLACK);
    display.drawRect(x_current_box, y_current_box, current_box_w, 40, GxEPD_BLACK);

    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Aujourd'hui", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_current_box + current_box_w / 2 - w / 2, y_current_box + 30);
    display.print("Aujourd'hui");
    displayCurrent(x_current_box, y_current_box, currentTemp, D0MinTemp, D0MaxTemp, weatherCodeToIcon(D0Code));

    // Forecast box for next 4 days
    x_forecast_box = 300;
    y_forecast_box = 60;
    forecast_box_w = 480;
    forecast_box_h = 210;
    display.fillRect(x_forecast_box, y_forecast_box, forecast_box_w, 40, GxEPD_GREEN);
    display.drawRect(x_forecast_box, y_forecast_box, forecast_box_w, forecast_box_h, GxEPD_BLACK);
    display.drawRect(x_forecast_box, y_forecast_box, forecast_box_w, 40, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Previsions", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_forecast_box + forecast_box_w / 2 - w / 2, y_forecast_box + 30);
    display.print("Previsions");

    displayForecast(x_forecast_box + 10, y_forecast_box + 40, (today + 1) % 7, D1MinTemp, D1MaxTemp, weatherCodeToIcon(D1Code));
    displayForecast(x_forecast_box + 120, y_forecast_box + 40, (today + 2) % 7, D2MinTemp, D2MaxTemp, weatherCodeToIcon(D2Code));
    displayForecast(x_forecast_box + 240, y_forecast_box + 40, (today + 3) % 7, D3MinTemp, D3MaxTemp, weatherCodeToIcon(D3Code));
    displayForecast(x_forecast_box + 360, y_forecast_box + 40, (today + 4) % 7, D4MinTemp, D4MaxTemp, weatherCodeToIcon(D4Code));

    // Home Assistant sensors box
    x_ha_box = 30;
    y_ha_box = 300;
    ha_box_w = 400;
    ha_box_h = 150;
    display.fillRect(x_ha_box, y_ha_box, ha_box_w, 40, GxEPD_GREEN);
    display.drawRect(x_ha_box, y_ha_box, ha_box_w, ha_box_h, GxEPD_BLACK);
    display.drawRect(x_ha_box, y_ha_box, ha_box_w, 40, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Capteurs Home Assistant", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + ha_box_w / 2 - w / 2, y_ha_box + 30);
    display.print("Capteurs Home Assistant");

    // Internal temperature from SHT4
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Interieur", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + ha_box_w / 6 - w / 2, y_ha_box + 70);
    display.print("Interieur");
    display.setFont(&FreeSans18pt7b);
    display.getTextBounds(String(sht4xTemperature, 1) + "`", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + ha_box_w / 6 - w / 2, y_ha_box + 120);
    display.print(sht4xTemperature, 1);
    display.write(0x60);

    // Outdoor temperature (Home Assistant)
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Exterieur", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + ha_box_w / 2 - w / 2, y_ha_box + 70);
    display.print("Exterieur");
    display.setFont(&FreeSans18pt7b);
    display.getTextBounds(String(externalTemperatureSensor, 1) + "`", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + ha_box_w / 2 - w / 2, y_ha_box + 120);
    display.print(externalTemperatureSensor, 1);
    display.write(0x60);

    // Greenhouse temperature (Home Assistant)
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Serre", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + 5 * ha_box_w / 6 - w / 2, y_ha_box + 70);
    display.print("Serre");
    display.setFont(&FreeSans18pt7b);
    display.getTextBounds(String(greenHouseTemperature, 1) + "`", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_ha_box + 5 * ha_box_w / 6 - w / 2, y_ha_box + 120);
    display.print(greenHouseTemperature, 1);
    display.write(0x60);

    // Bitcoin price box
    x_bitcoin_box = 460;
    y_bitcoin_box = 300;
    bitcoin_box_w = 150;
    bitcoin_box_h = 150;
    display.fillRect(x_bitcoin_box, y_bitcoin_box, bitcoin_box_w, 40, GxEPD_GREEN);
    display.drawRect(x_bitcoin_box, y_bitcoin_box, bitcoin_box_w, bitcoin_box_h, GxEPD_BLACK);
    display.drawRect(x_bitcoin_box, y_bitcoin_box, bitcoin_box_w, 40, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Crypto", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_bitcoin_box + bitcoin_box_w / 2 - w / 2, y_bitcoin_box + 30);
    display.print("Crypto");
    // Bitcoin
    display.drawBitmap(x_bitcoin_box+10, y_bitcoin_box + 60, epd_bitmap3_allArray[0], 30, 30, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.setCursor(x_bitcoin_box + 40, y_bitcoin_box + 80);
    display.print(btc);
    display.print(" $");
    // Ethereum
    display.drawBitmap(x_bitcoin_box+10, y_bitcoin_box + 95, epd_bitmap3_allArray[2], 30, 30, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.setCursor(x_bitcoin_box + 40, y_bitcoin_box + 120);
    display.print(eth);
    display.print(" $");


    // Battery percentage box
    x_battery_box = 640;
    y_battery_box = 300;
    battery_box_w = 140;
    battery_box_h = 150;
    display.fillRect(x_battery_box, y_battery_box, battery_box_w, 40, GxEPD_GREEN);
    display.drawRect(x_battery_box, y_battery_box, battery_box_w, battery_box_h, GxEPD_BLACK);
    display.drawRect(x_battery_box, y_battery_box, battery_box_w, 40, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.getTextBounds("Batterie", 0, 0, &x1, &y1, &w, &h);
    display.setCursor(x_battery_box + battery_box_w / 2 - w / 2, y_battery_box + 30);
    display.print("Batterie");
    display.drawBitmap(x_battery_box + 10, y_battery_box + 70, epd_bitmap3_allArray[1], 40, 40, GxEPD_BLACK);
    display.setFont(&FreeSans12pt7b);
    display.setCursor(x_battery_box + 50, y_battery_box + 80);
    display.print(vBatPercentage);
    display.print(" %");
    display.setCursor(x_battery_box + 50, y_battery_box + 120);
    display.print(vBat);
    display.print(" V");    

  } while (display.nextPage());

  display.hibernate();
  delay(1000);
  uint64_t sleepTime = 60 * 60 * uS_TO_S_FACTOR;  // 60 minutes
  esp_error = esp_sleep_enable_timer_wakeup(sleepTime);
  if (esp_error != ESP_OK) {
    Serial1.print("Error to enter deep sleep: ");
    Serial1.println(esp_error);
  } else {
    Serial1.println("Enter deep sleep for 60min...");
    esp_deep_sleep_start();
  }
}
