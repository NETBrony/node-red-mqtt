#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>     
#include <SHT31.h>      
#include "secret.h"     
#include "icon.h"       

// ==========================================
// ⚙️ PIN DEFINITIONS
// ==========================================
#define OLED_MOSI   23
#define OLED_CLK    18
#define OLED_DC     16
#define OLED_CS     5
#define OLED_RESET  17
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define RTC_SDA     21
#define RTC_SCL     22
#define SHT_SDA     27
#define SHT_SCL     14

#define PIN_ZMPT        34  
#define PIN_MENU_BTN    33  
#define PIN_RELAY_PUMP  25
#define PIN_RELAY_AUX   26

// ==========================================
// 🛠️ OBJECT INSTANTIATION
// ==========================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, 
  OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

RTC_DS3231 rtc;
SHT31 sht30(0x44, &Wire1); 

WiFiClientSecure espClient;
PubSubClient client(espClient);

#include "wifi-connect.h"

// ==========================================
// 📊 VARIABLES & TIMERS
// ==========================================
unsigned long prevMillisSensor = 0;
unsigned long prevMillisOled = 0;
unsigned long prevMillisOverload = 0;

const long intervalSensor = 2000;
const long intervalOled = 500; 
const long intervalOverload = 100; 
const unsigned long RESTART_INTERVAL = 30 * 60 * 1000; // 30 นาที

int currentMenuPage = 0; 
int lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;

float temperature = 0.0;
float humidity = 0.0;
bool isOverloadTrip = false; 
bool pumpState = false;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7 * 3600; 
const int   daylightOffset_sec = 0;

// ==========================================
// 🔄 HELPER FUNCTIONS
// ==========================================

void autoMaintenance() {
    if (millis() > RESTART_INTERVAL) {
        if (pumpState == false) {
            Serial.println("\n🧹 [MAINTENANCE] Cleaning RAM...");
            display.clearDisplay();
            display.setTextSize(1);
            display.setCursor(20, 30);
            display.print("System Cleaning...");
            display.display();
            delay(1000); 
            ESP.restart(); 
        }
    }
}

bool checkOverload() {
  int maxVal = 0;
  unsigned long startSample = millis();
  while(millis() - startSample < 20) { 
    int val = analogRead(PIN_ZMPT);
    if(val > maxVal) maxVal = val;
  }
  if (maxVal > 2000) return true; 
  return false;
}

void syncTimeNetwork() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  for(int i=0; i<3; i++){
    if (getLocalTime(&timeinfo)) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      Serial.println("✅ [TIME] Synced NTP to RTC");
      return;
    }
    delay(500);
  }
}

// -----------------------------------------------------------
// 🎨 UI: Helper เพื่อจัดข้อความกึ่งกลาง
// -----------------------------------------------------------
void printCentered(String text, int y, int size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

// -----------------------------------------------------------
// 🎨 UI: ส่วน Header (เวลา ซ้าย / MQTT ขวา)
// -----------------------------------------------------------
void drawHeader() {
  DateTime now = rtc.now();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  // 1. เวลา (มุมซ้ายบน)
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
  display.setCursor(0, 0); 
  display.print(timeStr);

  // 2. สถานะ MQTT (มุมขวาบน)
  String mqttStatus = client.connected() ? "MQTT:ON" : "MQTT:--";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(mqttStatus, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, 0); // ชิดขวา
  display.print(mqttStatus);
  
  // เส้นคั่น Header
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
}

// -----------------------------------------------------------
// 🎨 UI: ส่วน Footer (สถานะ Pump)
// -----------------------------------------------------------
void drawFooter() {
  // เส้นคั่น Footer
  display.drawLine(0, 52, SCREEN_WIDTH, 52, SSD1306_WHITE);
  
  // แสดงสถานะ Pump (กึ่งกลางด้านล่าง)
  String statusMsg = "PUMP: " + String(pumpState ? "ON" : "OFF");
  
  // ถ้า Overload ให้กระพริบ หรือขึ้นเตือน
  if (isOverloadTrip) statusMsg = "! TRIP !";
  
  printCentered(statusMsg, 56, 1);
}

// -----------------------------------------------------------
// 🎨 UI: รวมการแสดงผลทั้งหมด
// -----------------------------------------------------------
void updateDisplay() {
  display.clearDisplay();
  
  // วาดส่วนบนและล่าง
  drawHeader();
  drawFooter();

  // วาดส่วนเนื้อหา (Body) - ตรงกลาง
  // พื้นที่ y=12 ถึง y=50
  if (isOverloadTrip) {
      printCentered("OVERLOAD", 20, 2);
      printCentered("Check Mag", 40, 1);
  } else {
    if (currentMenuPage == 0) {
      // --- Page 1: Temperature ---
      printCentered("TEMPERATURE", 15, 1);
      
      // จัด Temp ให้อยู่กึ่งกลาง พร้อมองศา C
      display.setTextSize(3);
      String tempStr = String(temperature, 1);
      
      // คำนวณความกว้างรวม (ตัวเลข + องศา + C) เพื่อจัดกลางแม่นยำ
      int16_t x1, y1; uint16_t w, h, w_deg, h_deg, w_c, h_c;
      display.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
      
      // วาดตัวเลข
      int startX = (SCREEN_WIDTH - (w + 18 + 18)) / 2; // กะระยะคร่าวๆ
      display.setCursor(startX + 10, 25);
      display.print(tempStr);
      
      // วาด °C
      display.setTextSize(1); // องศาตัวเล็กหน่อยจะได้สวย
      display.setCursor(display.getCursorX() + 2, 25);
      display.cp437(true); // เปิดใช้ Extended ASCII
      display.write(248);  // รหัสวงกลม (Degree symbol)
      display.setTextSize(2);
      display.print("C");

    } else {
      // --- Page 2: Humidity ---
      printCentered("HUMIDITY", 15, 1);
      
      String humStr = String(humidity, 1) + " %";
      printCentered(humStr, 28, 3);
    }
  }

  display.display();
}

// ==========================================
// 📡 MQTT & CONTROL
// ==========================================
void sendPumpStatus(bool state, bool isLockout) {
  if (!client.connected()) return;
  String statusJson = "";
  if (isLockout) {
    statusJson = "LOCKED"; 
    client.publish(topic_error, "Pump Overload Tripped!", false);
  } else {
    statusJson = state ? "1" : "0";
  }
  client.publish(topic_light_status, statusJson.c_str(), true);
}

void controlPump(bool turnOn) {
  if (isOverloadTrip) {
      sendPumpStatus(false, true);
      return;
  }
  if (turnOn) {
    digitalWrite(PIN_RELAY_PUMP, HIGH); 
    pumpState = true;
    Serial.println("💦 [PUMP] ON");
  } else {
    digitalWrite(PIN_RELAY_PUMP, LOW);
    pumpState = false;
    Serial.println("💦 [PUMP] OFF");
  }
  sendPumpStatus(pumpState, false);
}

void callback(char *topic, byte *payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  if (String(topic) == topic_control) {
    if (message == "1") controlPump(true);
    else if (message == "0") controlPump(false);
  }
}

void reconnect() {
  if (!client.connected()) {
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      client.subscribe(topic_control);
      sendPumpStatus(pumpState, isOverloadTrip); 
    }
  }
}

// ==========================================
// 🚀 SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_MENU_BTN, INPUT_PULLUP); 
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_RELAY_AUX, OUTPUT);
  pinMode(PIN_ZMPT, INPUT); 
  digitalWrite(PIN_RELAY_PUMP, LOW); 

  // Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  
  // 🔥 [FIX] Boot Logo Logic
  // ใช้การวาดครั้งเดียว แล้ว delay แทนการ loop
  display.clearDisplay();
  display.drawBitmap((128-64)/2, 0, ruts_logo, 64, 64, SSD1306_WHITE); 
  display.display();
  Serial.println(">> [BOOT] Showing Logo 5s");
  delay(5000); // โชว์ค้างไว้ 5 วินาที
  
  display.clearDisplay();
  display.cp437(true); // 🔥 [FIX] เปิดใช้ Code Page 437 เพื่อโชว์เครื่องหมายองศา

  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) Serial.println("❌ RTC Error");

  Wire1.begin(SHT_SDA, SHT_SCL);
  if (!sht30.begin()) Serial.println("❌ SHT30 Error");

  display.setCursor(0,0); display.println("Connecting WiFi..."); display.display();
  setup_wifi_manager(); 

  display.println("WiFi Connected!");
  display.display();
  
  syncTimeNetwork();
  WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), IPAddress(8,8,8,8));

  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);      
  client.setSocketTimeout(60);  
}

// ==========================================
// 🔄 LOOP
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  if (WiFi.status() != WL_CONNECTED) ESP.restart();
  if (!client.connected()) reconnect();
  client.loop();

  autoMaintenance(); 

  if (currentMillis - prevMillisOverload >= intervalOverload) {
      prevMillisOverload = currentMillis;
      if (checkOverload()) {
        if (!isOverloadTrip) { 
          isOverloadTrip = true;
          controlPump(false); 
        }
      } else {
        if (isOverloadTrip) {
            isOverloadTrip = false;
            sendPumpStatus(false, false); 
        }
      }
  }

  if (currentMillis - prevMillisSensor >= intervalSensor) {
    prevMillisSensor = currentMillis;
    if (sht30.read()) {
      temperature = sht30.getTemperature();
      humidity = sht30.getHumidity();
      char msgBuffer[64];
      snprintf(msgBuffer, sizeof(msgBuffer), "{\"temp\":%.1f,\"humi\":%.1f}", temperature, humidity);
      client.publish(topic_TempHumi, msgBuffer);
    }
  }

  int reading = digitalRead(PIN_MENU_BTN);
  if (reading != lastBtnState) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > 50) {
      static int buttonState = HIGH;
      if (reading != buttonState) {
          buttonState = reading;
          if (buttonState == LOW) { 
              currentMenuPage++;
              if (currentMenuPage > 1) currentMenuPage = 0;
              updateDisplay(); 
          }
      }
  }
  lastBtnState = reading;

  if (currentMillis - prevMillisOled >= intervalOled) {
    prevMillisOled = currentMillis;
    updateDisplay();
  }
}