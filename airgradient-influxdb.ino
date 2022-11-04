/*
This is the code for the AirGradient DIY Air Quality Sensor with an ESP8266 Microcontroller.

It is a high quality sensor showing PM2.5, CO2, Temperature and Humidity on a small display and can send data over Wifi.

For build instructions please visit https://www.airgradient.com/diy/

Compatible with the following sensors:
Plantower PMS5003 (Fine Particle Sensor)
SenseAir S8 (CO2 Sensor)
SHT30/31 (Temperature/Humidity Sensor)

Please install ESP8266 board manager (tested with version 3.0.0)

The codes needs the following libraries installed:

<I actually forgot which ones...sorry>

If you have any questions please visit our forum at https://forum.airgradient.com/


MIT License
*/

#include <AirGradient.h>

#include <ESP8266WiFi.h>

// My kit must have a jankier display than the others because I had to downgrade
// this library for a lower resolution display
#include <Wire.h>
#include "SH1106Wire.h"

#include <InfluxDbClient.h>

const char* deviceId = "airgradient";

// WiFi and IP connection info.
const char* ssid = "";
const char* password = "";

// set sensors that you do not use to false
boolean hasPM = true;
boolean hasCO2 = true;
boolean hasSHT = true;

// set to true if you want to connect to wifi. The display will show values only when the sensor has wifi connection
boolean connectWIFI = true;

// InfluxDB v1
#define INFLUXDB_URL ""
#define INFLUXDB_DB_NAME ""

// Set timezone string according to https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html
// Examples:
//  Pacific Time: "PST8PDT"
//  Eastern: "EST5EDT"
//  Japanesse: "JST-9"
//  Central Europe: "CET-1CEST,M3.5.0,M10.5.0/3"
#define TZ_INFO "PST8PDT"

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_DB_NAME);

AirGradient ag = AirGradient();

SH1106Wire display(0x3c, SDA, SCL);

// Data points
Point sensor("home");

void setup() {

  Serial.begin(9600);

  display.init();
  display.flipScreenVertically();
  showTextRectangle("Init", String(ESP.getChipId(), HEX), true);

  if (hasPM) ag.PMS_Init();
  if (hasCO2) ag.CO2_Init();
  if (hasSHT) ag.TMP_RH_Init(0x44);

  if (connectWIFI) {
    WiFi.mode(WIFI_STA);
    wifi_station_set_hostname(deviceId);
    connectToWifi();
  }

  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  // Add tags
  sensor.addTag("device", String(ESP.getChipId(), HEX));
  sensor.addTag("SSID", WiFi.SSID());

  // Accurate time is necessary for certificate validation and writing in batches
  // For the fastest time sync find NTP servers in your area: https://www.pool.ntp.org/zone/
  // Syncing progress and the time will be printed to Serial.
  timeSync(TZ_INFO, "us.pool.ntp.org", "time.nis.gov");

  delay(2000);
}

void loop() {

  // Clear fields for reusing the point. Tags will remain untouched
  sensor.clearFields();

  if (hasPM) {
    int PM2 = ag.getPM2_Raw();

    // Report PM2.5
    sensor.addField("PM2", PM2);
    sensor.addField("AQI", PM_TO_AQI_US(PM2));

    showTextRectangle("AQI", String(PM_TO_AQI_US(PM2)), false);

    delay(3000);
  }

  if (hasCO2) {
    int CO2 = ag.getCO2_Raw();

    // Report CO2
    sensor.addField("CO2", CO2);

    showTextRectangle("CO2", String(CO2), false);
    delay(3000);
  }

  if (hasSHT) {
    TMP_RH result = ag.periodicFetchData();

    // Report temperature and humidity
    sensor.addField("tmpc", result.t);
    sensor.addField("tmpf", (result.t * 9 / 5)+32);
    sensor.addField("rh", result.rh);

      showTextRectangle(String((result.t * 9 / 5) + 32), String(result.rh) + "%", false);

    delay(3000);
  }

  // send payload
  if (connectWIFI) {
    // Print what are we exactly writing
    Serial.print("Writing: ");
    Serial.println(sensor.toLineProtocol());

    // If no Wifi signal, try to reconnect it
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Wifi connection lost");
      WiFi.reconnect();
    }


    // Write point
    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
      WiFi.reconnect();
    }

    delay(1000);
  }
}

// DISPLAY
void showTextRectangle(String ln1, String ln2, boolean small)
{
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  if (small) {
    display.setFont(ArialMT_Plain_16);
  } else {
    display.setFont(ArialMT_Plain_24);
  }
  display.drawString(32, 16, ln1);
  display.drawString(32, 36, ln2);
  display.display();
}

// Wifi Manager
void connectToWifi()
{
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    showTextRectangle("Trying to", "connect...", true);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Hostname: ");
  Serial.println(WiFi.hostname());
}

// Calculate PM2.5 US AQI
int PM_TO_AQI_US(int pm02)
{
  if (pm02 <= 12.0) return ((50 - 0) / (12.0 - .0) * (pm02 - .0) + 0);
  else if (pm02 <= 35.4) return ((100 - 50) / (35.4 - 12.0) * (pm02 - 12.0) + 50);
  else if (pm02 <= 55.4) return ((150 - 100) / (55.4 - 35.4) * (pm02 - 35.4) + 100);
  else if (pm02 <= 150.4) return ((200 - 150) / (150.4 - 55.4) * (pm02 - 55.4) + 150);
  else if (pm02 <= 250.4) return ((300 - 200) / (250.4 - 150.4) * (pm02 - 150.4) + 200);
  else if (pm02 <= 350.4) return ((400 - 300) / (350.4 - 250.4) * (pm02 - 250.4) + 300);
  else if (pm02 <= 500.4) return ((500 - 400) / (500.4 - 350.4) * (pm02 - 350.4) + 400);
  else return 500;
};
