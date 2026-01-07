#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SHT31.h>      
#include "secret.h"     

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// ⚙️ PIN DEFINITIONS
// ==========================================
#define SHT_SDA         21  
#define SHT_SCL         22  
#define PIN_LED_PUMP    19  // Output: คุมไฟ/ปั๊ม
#define PIN_MANUAL_SW   17  // Input: ปุ่มกด
#define PIN_ZMPT        34  // Input: ตรวจสอบสถานะ (Feedback)

// Logic LED: Active HIGH
#define PUMP_ON   HIGH  
#define PUMP_OFF  LOW   

// ==========================================
// 🛠️ OBJECTS
// ==========================================
SHT31 sht30(0x44); 
WiFiClientSecure espClient;
PubSubClient client(espClient);

#include "wifi-connect.h"

// ==========================================
// 📊 VARIABLES
// ==========================================
unsigned long prevMillisSensor = 0;
unsigned long prevMillisFault = 0;

// WiFi Interval
unsigned long previousMillisWiFi = 0;
const long intervalWiFi = 30000; 

const long intervalSensor = 2000;
const long intervalFaultCheck = 100; 

// Fault Config (Overload + Open Circuit)
unsigned long faultStartTime = 0;
const long FAULT_CONFIRM_TIME = 2000; // ต้องผิดปกตินาน 2 วินาทีถึงจะตัด
bool isFaultPending = false; 

// Thresholds
const int ZMPT_OVERLOAD_THRESHOLD = 3000; // ค่าสูงเกิน = Overload (ไฟกระชาก)
const int OPEN_CIRCUIT_THRESHOLD = 500;   // ค่าต่ำกว่านี้ตอนเปิดปั๊ม = สายขาด (Open Circuit)

// Startup Delay
unsigned long systemReadyTime = 0;
const unsigned long STARTUP_DELAY_MS = 5000; 

// Button
int lastBtnState = HIGH;
unsigned long lastDebounceTime = 0;

float temperature = 0.0;
float humidity = 0.0;
bool isTrip = false;      // เปลี่ยนชื่อจาก isOverloadTrip ให้สื่อความหมายรวมๆ
bool pumpState = false;

// ==========================================
// 🔄 FAULT CHECK FUNCTION (หัวใจสำคัญ)
// ==========================================
bool checkSystemFault() {
  int maxVal = 0;
  int minVal = 4095;
  long sumVal = 0;
  int samples = 0;
  unsigned long startSample = millis();
  
  // เก็บตัวอย่าง 20ms
  while(millis() - startSample < 20) { 
    int val = analogRead(PIN_ZMPT);
    if(val > maxVal) maxVal = val;
    if(val < minVal) minVal = val;
    sumVal += val;
    samples++;
  }
  
  int amplitude = maxVal - minVal; // สำหรับเช็ค ZMPT (AC Signal)
  int avgVal = sumVal / samples;   // สำหรับเช็ค LED (DC Signal)

  // Debug: ดูค่าผ่าน Serial เพื่อจูน
  // Serial.printf("Amp: %d, Avg: %d, PumpState: %d\n", amplitude, avgVal, pumpState);

  // 🚩 กรณี 1: Overload (กระแสสูงเกิน)
  // ถ้าใช้ ZMPT วัดไฟ AC ค่า Amplitude จะสูง
  if (amplitude > ZMPT_OVERLOAD_THRESHOLD) { 
    Serial.println("⚠️ Fault: High Current Detected!");
    return true; 
  }

  // 🚩 กรณี 2: Open Circuit (สั่งเปิดแต่ไฟไม่มา)
  // เช็คเฉพาะตอนที่สั่งเปิดปั๊ม (pumpState == true)
  if (pumpState == true) {
      // ถ้าสั่งเปิดแล้ว แต่ค่าเฉลี่ยที่อ่านได้ต่ำมาก (ใกล้ 0)
      if (avgVal < OPEN_CIRCUIT_THRESHOLD) {
          Serial.println("⚠️ Fault: Open Circuit / LED Broken!");
          return true;
      }
  }

  return false;
}

// ==========================================
// 📡 MQTT & CONTROL
// ==========================================
void sendPumpStatus(bool state, bool isLockout) {
  if (!client.connected()) return;
  
  if(isLockout) {
    client.publish(topic_error, "System Tripped! (Overload/Open)", false);
    client.publish(topic_light_status, "LOCKED", true);
  } else {
    client.publish(topic_light_status, state ? "1" : "0", true);
  }
}

void controlPump(bool turnOn) {
  if (isTrip) { 
    sendPumpStatus(false, true); 
    return; 
  }

  digitalWrite(PIN_LED_PUMP, turnOn ? PUMP_ON : PUMP_OFF);
  pumpState = turnOn;
  sendPumpStatus(pumpState, false);
}

void callback(char *topic, byte *payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.print("📩 [MQTT] "); Serial.print(topic); Serial.print(": "); Serial.println(message);
  
  if (String(topic) == topic_control) {
    message.trim();
    if (message == "1") controlPump(true);
    else if (message == "0") controlPump(false);
  }
}

void reconnect() {
  if (!client.connected()) {
    String clientId = "ESP32-Dev-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      client.subscribe(topic_control);
      sendPumpStatus(pumpState, isTrip); 
    }
  }
}

// ==========================================
// 🚀 SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  Serial.begin(115200);

  // 1. Config Pins
  pinMode(PIN_MANUAL_SW, INPUT_PULLUP); 
  pinMode(PIN_LED_PUMP, OUTPUT);
  pinMode(PIN_ZMPT, INPUT); // ⚠️ อย่าลืมต่อ R 10k Pull-down ที่ขานี้ลง GND

  digitalWrite(PIN_LED_PUMP, PUMP_OFF); 

  // 2. Init Sensors
  Wire.begin(SHT_SDA, SHT_SCL); 
  if (!sht30.begin()) {   
    Serial.println("❌ SHT30 Error");
  }

  // 3. Network
  setup_wifi_manager(); 

  // 4. MQTT
  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setKeepAlive(60);      
  client.setSocketTimeout(60);  

  systemReadyTime = millis() + STARTUP_DELAY_MS;
  Serial.println("🚀 System Ready");
}

// ==========================================
// 🔄 LOOP
// ==========================================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Network Check
  if (currentMillis - previousMillisWiFi >= intervalWiFi) {
    previousMillisWiFi = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(); 
      WiFi.reconnect();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) reconnect();
    client.loop();
  }

  // 2. Fault Logic (Overload + Open Circuit)
  if (currentMillis - prevMillisFault >= intervalFaultCheck) {
      prevMillisFault = currentMillis;

      if (millis() > systemReadyTime) { 
          bool faultDetected = checkSystemFault(); 

          if (faultDetected) {
              if (!isTrip) {
                  if (!isFaultPending) {
                      isFaultPending = true;
                      faultStartTime = currentMillis;
                      Serial.println("⚠️ Fault Pending...");
                  } else {
                      // ถ้าผิดปกตินานเกิน 2 วินาที -> ตัดระบบ
                      if (currentMillis - faultStartTime >= FAULT_CONFIRM_TIME) {
                          isTrip = true;
                          isFaultPending = false;
                          controlPump(false); // Force OFF
                          Serial.println("🚨 SYSTEM TRIPPED! Check Wiring/Load."); 
                          sendPumpStatus(false, true); 
                      }
                  }
              }
          } else {
              isFaultPending = false; 
              // ถ้า Fault หายไป (เช่น เสียบสายคืน)
              // ปกติระบบ Safety จะไม่ Auto Reset แต่ถ้าอยากให้ Reset เองได้
              // ให้ uncomment บรรทัดล่างนี้ครับ
              /*
              if (isTrip) {
                 isTrip = false;
                 Serial.println("✅ Fault Cleared.");
                 sendPumpStatus(false, false); 
              }
              */
          }
      }
  }

  // 3. Sensor Reading
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

  // 4. Manual Switch (กดเพื่อเปิด/ปิด และ Reset Trip)
  int reading = digitalRead(PIN_MANUAL_SW);
  if (reading != lastBtnState) lastDebounceTime = currentMillis;
  
  if ((currentMillis - lastDebounceTime) > 50) {
      static int buttonState = HIGH;
      if (reading != buttonState) {
          buttonState = reading;
          if (buttonState == LOW) { 
              // ถ้ากดปุ่มตอน Trip อยู่ ให้ Reset ระบบได้
              if (isTrip) {
                  isTrip = false;
                  Serial.println("🔄 Manual Reset Trip");
                  sendPumpStatus(false, false);
              } else {
                  // ถ้าปกติ ให้ Toggle เปิดปิด
                  controlPump(!pumpState); 
              }
          }
      }
  }
  lastBtnState = reading;
}