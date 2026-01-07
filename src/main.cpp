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
#include "icon.h" // ✅ เรียกใช้ Logo จากไฟล์นี้

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
// 📊 VARIABLES
// ==========================================
unsigned long prevMillisSensor = 0;
unsigned long prevMillisOled = 0;
unsigned long prevMillisOverload = 0;

const long intervalSensor = 2000;
const long intervalOled = 500; 
const long intervalOverload = 100; 
const unsigned long RESTART_INTERVAL = 30 * 60 * 1000;

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
// 🎨 BOOT UI FUNCTION (ปรับปรุงใหม่)
// ==========================================
// ฟังก์ชันนี้จะวาด Logo ค้างไว้ แล้วเปลี่ยนข้อความด้านล่าง
void updateBootStatus(String statusMsg) {
    display.clearDisplay(); 

    // 1. วาด Logo (ขยับลงมาที่ Y=5 เพื่อความสวยงาม)
    // Logo สูง 35px, วางที่ 5, จบที่ 40. เหลือที่ด้านล่าง 24px
    display.drawBitmap(0, 5, ruts_logo, 128, 35, SSD1306_WHITE);

    // 2. วาดข้อความสถานะ (Status Text)
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // คำนวณจัดกึ่งกลางข้อความ
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(statusMsg, 0, 0, &x1, &y1, &w, &h);
    int x_text = (SCREEN_WIDTH - w) / 2;
    int y_text = 50; // วางไว้ใต้ Logo (Logo จบที่ ~40)

    display.setCursor(x_text, y_text);
    display.print(statusMsg);

    // 3. วาดเส้น Loading bar เล็กๆ ด้านล่างสุด (Optional)
    static int barWidth = 0;
    barWidth += 20; 
    if(barWidth > 128) barWidth = 0;
    display.drawFastHLine(0, 62, barWidth, SSD1306_WHITE);

    display.display();
}

// ==========================================
// 🔄 HELPER FUNCTIONS
// ==========================================
void autoMaintenance() {
    if (millis() > RESTART_INTERVAL) {
        if (pumpState == false) {
            updateBootStatus("Maintenance...");
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
  
  // ลอง Sync 5 ครั้ง (พร้อมอัปเดตหน้าจอ)
  for(int i=0; i<5; i++){
    updateBootStatus("Syncing Time (" + String(i+1) + "/5)");
    if (getLocalTime(&timeinfo)) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec));
      updateBootStatus("Time Synced! OK");
      delay(500);
      return;
    }
    delay(500); 
  }
  updateBootStatus("Time Sync Failed");
  delay(1000);
}

// ==========================================
// 🎨 MAIN UI Functions
// ==========================================
void drawHeader() {
  DateTime now = rtc.now();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
  display.setCursor(0, 0); 
  display.print(timeStr);

  String mqttStatus = client.connected() ? "MQTT:ON" : "MQTT:--";
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(mqttStatus, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(SCREEN_WIDTH - w, 0); 
  display.print(mqttStatus);
  
  display.drawLine(0, 10, SCREEN_WIDTH, 10, SSD1306_WHITE);
}

void drawFooter() {
  // ✅ ขยับเส้นลงไปที่ Y=56 (เกือบสุดขอบ)
  display.drawLine(0, 54, SCREEN_WIDTH, 54, SSD1306_WHITE);
  
  String statusMsg = "PUMP: " + String(pumpState ? "ON" : "OFF");
  if (isOverloadTrip) statusMsg = "! TRIP !";
  
  display.setTextSize(1);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(statusMsg, 0, 0, &x1, &y1, &w, &h);
  
  // ✅ ขยับตัวหนังสือลงไปที่ Y=57 (ต่ำสุดเท่าที่จะทำได้โดยไม่ตกขอบ)
  display.setCursor((SCREEN_WIDTH - w) / 2, 57);
  display.print(statusMsg);
}

void updateDisplay() {
  display.clearDisplay();
  drawHeader();
  drawFooter();

  if (isOverloadTrip) {
      display.setTextSize(2);
      int16_t x1, y1; uint16_t w, h;
      String msg = "OVERLOAD";
      display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 22); // ขยับลง
      display.print(msg);

      display.setTextSize(1);
      msg = "Check Mag/Pump";
      display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
      display.setCursor((SCREEN_WIDTH - w) / 2, 42); // ขยับลง
      display.print(msg);
  } else {
    // ------------------------------------
    // PAGE 0: TEMPERATURE
    // ------------------------------------
    if (currentMenuPage == 0) { 
      display.setTextSize(1);
      String title = "TEMPERATURE";
      
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
      // ✅ ขยับหัวข้อลงมาที่ Y=18 (เดิม 16)
      display.setCursor((SCREEN_WIDTH - w) / 2, 16); 
      display.print(title);
      
      display.setTextSize(3);
      String tempStr = String(temperature, 1);
      display.getTextBounds(tempStr, 0, 0, &x1, &y1, &w, &h);
      int startX = (SCREEN_WIDTH - (w + 24)) / 2; 
      
      // ✅ ขยับตัวเลขลงมาที่ Y=30 (เดิม 28) ให้ดูอยู่กึ่งกลางพื้นที่ว่างมากขึ้น
      display.setCursor(startX, 30); 
      display.print(tempStr);
      
      display.setTextSize(1); 
      display.setCursor(display.getCursorX() + 4, 30);
      display.write(248); 
      display.setTextSize(2);
      display.print("C");
    } 
    // ------------------------------------
    // PAGE 1: HUMIDITY
    // ------------------------------------
    else if (currentMenuPage == 1) { 
      display.setTextSize(1);
      String title = "HUMIDITY";
      int16_t x1, y1; uint16_t w, h;
      display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
      
      display.setCursor((SCREEN_WIDTH - w) / 2, 16); // ✅ Y=18
      display.print(title);
      
      display.setTextSize(3);
      String humStr = String(humidity, 1) + "%";
      display.getTextBounds(humStr, 0, 0, &x1, &y1, &w, &h);
      
      display.setCursor((SCREEN_WIDTH - w) / 2, 30); // ✅ Y=30
      display.print(humStr);
    }
    // ------------------------------------
    // PAGE 2: WIFI INFO
    // ------------------------------------
    else { 
      // ขยับทุกบรรทัดลงมาอีกนิด ให้กระจายเต็มจอ
      display.setTextSize(1);
      display.setCursor(0, 14); // เดิม 15
      display.println("WiFi STATUS:");
      
      display.setCursor(0, 25); // เดิม 26
      display.print("SSID: ");
      String ssid = WiFi.SSID();
      if(ssid.length() > 10) ssid = ssid.substring(0, 10) + ".."; 
      display.print(ssid);
      
      display.setCursor(0, 35); // เดิม 36
      display.print("IP: ");
      display.print(WiFi.localIP());
      
      display.setCursor(0, 45); // เดิม 46
      display.print("Sig: ");
      display.print(WiFi.RSSI());
      display.print(" dBm");
    }
  }
  display.display();
}

// ==========================================
// 📡 MQTT & CONTROL
// ==========================================
void sendPumpStatus(bool state, bool isLockout) {
  if (!client.connected()) return;
  String statusJson = isLockout ? "LOCKED" : (state ? "1" : "0");
  if(isLockout) client.publish(topic_error, "Pump Overload Tripped!", false);
  client.publish(topic_light_status, statusJson.c_str(), true);
}

void controlPump(bool turnOn) {
  if (isOverloadTrip) { sendPumpStatus(false, true); return; }
  digitalWrite(PIN_RELAY_PUMP, turnOn ? HIGH : LOW);
  pumpState = turnOn;
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
    if (client.connect(("ESP32-" + String(random(0xffff), HEX)).c_str(), mqtt_user, mqtt_pass)) {
      client.subscribe(topic_control);
      sendPumpStatus(pumpState, isOverloadTrip); 
    }
  }
}

// ==========================================
// 🚀 SETUP (The Masterpiece)
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_MENU_BTN, INPUT_PULLUP); 
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_RELAY_AUX, OUTPUT);
  pinMode(PIN_ZMPT, INPUT); 
  digitalWrite(PIN_RELAY_PUMP, LOW); 

  if(!display.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println(F("SSD1306 failed"));
    for(;;);
  }
  
  display.cp437(true); // ใช้ Code page 437 เพื่อแสดงสัญลักษณ์พิเศษ

  // --------------------------------------------------
  // STEP 1: System Booting
  // --------------------------------------------------
  updateBootStatus("System Booting...");
  delay(1000); // โชว์ Logo + Booting แป๊บนึง

  Wire.begin(RTC_SDA, RTC_SCL);
  if (!rtc.begin()) Serial.println("❌ RTC Error");
  Wire1.begin(SHT_SDA, SHT_SCL);
  if (!sht30.begin()) Serial.println("❌ SHT30 Error");

  // --------------------------------------------------
  // STEP 2: WiFi Connection
  // --------------------------------------------------
  updateBootStatus("Connecting WiFi...");
  
  setup_wifi_manager(); // ฟังก์ชันนี้จะ Block จนกว่าจะต่อติด
  
  updateBootStatus("WiFi Connected!");
  delay(1000);

  // --------------------------------------------------
  // STEP 3: Time Sync
  // --------------------------------------------------
  // ในฟังก์ชันนี้ผมใส่ updateBootStatus ไว้ข้างในแล้ว เพื่อให้เห็น Progress
  syncTimeNetwork(); 

  // --------------------------------------------------
  // STEP 4: MQTT Connection
  // --------------------------------------------------
  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);      
  client.setSocketTimeout(60);  

  unsigned long startMqtt = millis();
  while (!client.connected()) {
      updateBootStatus("Connecting MQTT..."); 
      
      String clientId = "ESP32-" + String(random(0xffff), HEX);
      if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
          updateBootStatus("MQTT Connected!");
          delay(500);
          client.subscribe(topic_control);
          sendPumpStatus(pumpState, isOverloadTrip);
      }
      
      if (client.connected()) break;
      if (millis() - startMqtt > 10000) {
          updateBootStatus("MQTT Skipped");
          delay(1000);
          break; 
      }
      delay(500);
  }
  
  // --------------------------------------------------
  // STEP 5: Ready -> Enter Loop
  // --------------------------------------------------
  updateBootStatus("System Ready");
  delay(500);

  display.clearDisplay();
  display.display();
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

  // --- OVERLOAD CHECK ---
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

  // --- SENSOR READ ---
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

  // --- BUTTON & MENU ---
  int reading = digitalRead(PIN_MENU_BTN);
  if (reading != lastBtnState) lastDebounceTime = currentMillis;
  
  if ((currentMillis - lastDebounceTime) > 50) {
      static int buttonState = HIGH;
      if (reading != buttonState) {
          buttonState = reading;
          if (buttonState == LOW) { 
              currentMenuPage++;
              if (currentMenuPage > 2) currentMenuPage = 0;
              updateDisplay(); 
          }
      }
  }
  lastBtnState = reading;

  // --- DISPLAY REFRESH ---
  if (currentMillis - prevMillisOled >= intervalOled) {
    prevMillisOled = currentMillis;
    updateDisplay();
  }
}