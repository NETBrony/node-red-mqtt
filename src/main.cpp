#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h"
#include "wifi-connect.h"  // ✅ เรียกใช้ Wifi Manager ที่เราสร้างไว้

// ==========================================
// CONFIGURATION & PINS
// ==========================================
// ⚠️ เปลี่ยนเป็น int เพื่อให้ wifi.h มองเห็น (extern)
int led = 2;            // LED สถานะ (Onboard)

#define light 19        // Relay / Magnetic Contactor
#define switchPin 17    // สวิตช์มือ (Manual)
#define feedbackPin 34  // ขาเช็คกระแส/แรงดัน (Feedback)

const int delay_ms = 500; // เวลาหน่วงไฟกระพริบ Standby
const unsigned long interval_sensor = 10000; 
const unsigned long lightCheckInterval = 100; 

// ==========================================
// VARIABLES
// ==========================================
unsigned long previousMillis = 0;
unsigned long lastLightCheck = 0;

// SHT31 Sensor
#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);
float temperature = 0.0;
float humidity = 0.0;

// System State
int lightState = LOW;
int buttonState;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// Network
WiFiClientSecure espClient; 
PubSubClient client(espClient);

// ==========================================
// LED FUNCTIONS
// ==========================================

// 1. ไฟกระพริบตอนเริ่มระบบ
void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH);
    delay(speed);
    digitalWrite(led, LOW);
    delay(speed);
  }
}

// 2. ไฟกระพริบรอ WiFi (Standby) - ใช้ร่วมกับ wifi.h
void ledStandby() {
  digitalWrite(led, HIGH);
  delay(delay_ms);
  digitalWrite(led, LOW);
  delay(delay_ms);
}

// 3. ไฟกระพริบรอ MQTT (Pending)
void mqttPending() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(led, HIGH);
    delay(100);
    digitalWrite(led, LOW);
    delay(100);
  }
  delay(2000); // เว้นจังหวะ 2 วินาที
}

// ==========================================
// HARDWARE CHECK (Feedback Logic)
// ==========================================
bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  if (sensorValue > 1000) return true;
  return false;
}

// ==========================================
// SEND STATUS TO REACT
// ==========================================
void sendLightStatus(String status) {
  if (!client.connected()) return;
  // ใช้ topic จาก secret.h จะดีกว่า Hardcode
  client.publish(topic_light_status, status.c_str(), true);
  Serial.print(">> [MQTT SEND] Light Status: ");
  Serial.println(status);
}

// ==========================================
// CONTROL LOGIC
// ==========================================
void controlLight(bool turnOn) {
  Serial.println("\n--------------------------------");
  if (turnOn) {
    Serial.println(">> [ACTION] Attempting to turn ON...");
    digitalWrite(light, HIGH);
    lightState = HIGH;

    delay(100); // รอไฟวิ่ง 100ms

    int val = analogRead(feedbackPin);
    Serial.print(">> [CHECK] Feedback Value: ");
    Serial.println(val);

    if (val > 1000) {
      Serial.println("✅ [SUCCESS] Load Detected. System Normal.");
      sendLightStatus("1");
    } else {
      Serial.println("🚨 [FAILURE] No Load Detected! (TRIP ACTIVATED)");
      Serial.println(">> [SAFETY] Cutting Power immediately.");
      
      digitalWrite(light, LOW);
      lightState = LOW;
      
      sendLightStatus("0");
      client.publish("sensor/error", "TRIP: Wire Broken/No Load", false);
    }
  } else {
    Serial.println(">> [ACTION] Turning OFF (Manual/Command)");
    digitalWrite(light, LOW);
    lightState = LOW;
    sendLightStatus("0");
  }
  Serial.println("--------------------------------\n");
}

// ==========================================
// MQTT CALLBACK
// ==========================================
void callback(char *topic, byte *payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];

  Serial.print("\n📩 [MQTT RECV] Topic: ");
  Serial.print(topic);
  Serial.print(" | Payload: ");
  Serial.println(message);

  if (String(topic) == "api/control") {
    if (message == "1") controlLight(true); 
    else if (message == "0") controlLight(false);
  }
}

// ==========================================
// SENSOR READING
// ==========================================
void readSensor() {
  if (sht30.read()) {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();

    char msgBuffer[64];
    snprintf(msgBuffer, sizeof(msgBuffer), "{\"temp\":%.1f,\"humi\":%.1f}", temperature, humidity);
    
    // ใช้ topic จาก secret.h
    client.publish(topic_TempHumi, msgBuffer);
    Serial.print("🌡️ [SENSOR] Updated: ");
    Serial.println(msgBuffer);

    if (lightState == HIGH) sendLightStatus("1");
    else sendLightStatus("0");

  } else {
    Serial.println("❌ [ERROR] SHT30 Read Failed!");
  }
}

// ==========================================
// MQTT RECONNECT
// ==========================================
void reconnect() {
  while (!client.connected()) {
    
    // Safety check: ถ้า WiFi หลุด ให้หลุดจาก Loop เพื่อไป Restart
    if(WiFi.status() != WL_CONNECTED) return;

    Serial.print(">> [MQTT] Connecting to Broker...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass, "sensor/connection", 0, true, "OFFLINE")) {
      Serial.println(" Connected! ✅");
      
      // ✅ ต่อติดแล้วให้ไฟติดค้าง
      digitalWrite(led, HIGH); 

      client.publish("sensor/connection", "ONLINE", true);
      Serial.println(">> [LWT] Status sent: ONLINE");

      client.subscribe("api/control");
      Serial.println(">> [SUB] Subscribed to 'api/control'");
      
      if (lightState == HIGH) sendLightStatus("1");
      else sendLightStatus("0");

    } else {
      Serial.print(" Failed (rc=");
      Serial.print(client.state());
      Serial.println(") Try again...");
      
      // ✅ เรียกใช้ไฟกระพริบรอ MQTT (Pending)
      mqttPending(); 
    }
  }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  
  pinMode(led, OUTPUT);
  pinMode(light, OUTPUT);
  pinMode(feedbackPin, INPUT);
  pinMode(switchPin, INPUT_PULLUP);

  Serial.println("\n\n=================================");
  Serial.println("   SMART FARM PRO - SYSTEM START");
  Serial.println("=================================");
  
  // ✅ เรียกใช้ไฟกระพริบเริ่มระบบ
  ledStart(led, 2, 250); 
  delay(1000);
  
  Wire.begin();
  if (sht30.begin()) Serial.println(">> [INIT] SHT30 Sensor: OK");
  else Serial.println("❌ [INIT] SHT30 Sensor: NOT FOUND");

  // ================================================
  // 🚀 เปลี่ยนจาก setup_wifi() เป็น WiFiManager
  // ================================================
  setup_wifi_manager(); // เรียกฟังก์ชันจาก wifi.h

  // ตั้งค่า MQTT
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);
  
  Serial.println(">> [INIT] System Ready. Waiting for commands...\n");
}

void loop() {
  // 1. ตรวจสอบสถานะ WiFi (Watchdog)
  if (WiFi.status() != WL_CONNECTED) {
     Serial.println("⚠️ [WiFi] Lost Connection! Restarting in 3 seconds...");
     delay(3000);
     ESP.restart(); // รีสตาร์ทระบบเพื่อให้ WiFiManager ทำงานใหม่ (เสถียรกว่าการพยายามต่อเอง)
  }

  // 2. ตรวจสอบ MQTT
  if (!client.connected()) reconnect();
  client.loop();

  // 3. Manual Switch
  int reading = digitalRead(switchPin);
  if (reading != lastButtonState) lastDebounceTime = millis();
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("\n🔘 [MANUAL] Physical Button Pressed");
        controlLight(!lightState);
      }
    }
  }
  lastButtonState = reading;

  // 4. Sensor Loop
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval_sensor) {
    previousMillis = currentMillis;
    readSensor();
  }

  // 5. Safety Watchdog
  if (currentMillis - lastLightCheck >= lightCheckInterval) {
    lastLightCheck = currentMillis;
    if (lightState == HIGH) {
      if (!isLightReallyOn()) {
        Serial.println("\n🚨 [WATCHDOG] Critical Failure! Current lost during operation.");
        digitalWrite(light, LOW);
        lightState = LOW;
        sendLightStatus("0");
        client.publish("sensor/error", "TRIP: Watchdog Cut", false);
      }
    }
  }
}