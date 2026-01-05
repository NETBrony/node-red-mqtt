#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>  // ✅ [แก้จุดที่ 1] เพิ่มบรรทัดนี้กลับเข้ามา
#include <Wire.h>
#include <SHT31.h>
#include "secret.h"
#include "wifi-connect.h"

// ==========================================
// ⚙️ CONFIGURATION & PINS
// ==========================================

// ⚠️ สำคัญ: ถ้าทดสอบเปล่าๆ ไม่มีโหลด ให้แก้เป็น false
const bool ENABLE_FEEDBACK_PROTECTION = false; 

int led = 2;            // LED สถานะ (Onboard)

#define light 19        // Relay / Magnetic Contactor
#define switchPin 17    // สวิตช์มือ (Manual Button)
#define feedbackPin 34  // ขาเช็คกระแส/แรงดัน (Feedback)

const int delay_ms = 500; 
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
// [SECTION 1] LED FUNCTIONS
// ==========================================
void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH);
    delay(speed);
    digitalWrite(led, LOW);
    delay(speed);
  }
}

void ledStandby() {
  digitalWrite(led, HIGH);
  delay(delay_ms);
  digitalWrite(led, LOW);
  delay(delay_ms);
}

void mqttPending() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(led, HIGH);
    delay(100);
    digitalWrite(led, LOW);
    delay(100);
  }
  delay(2000); 
}

// ==========================================
// [SECTION 2] HARDWARE CHECK (Feedback Logic)
// ==========================================
// ✅ [แก้จุดที่ 2] เติมฟังก์ชันนี้กลับเข้ามาครับ
bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  // ค่า Analog ของ ESP32 คือ 0-4095
  // ถ้ามีไฟไหลผ่าน ค่าควรอ่านได้มากกว่า 1000 (ปรับตาม HW จริง)
  if (sensorValue > 1000) return true;
  return false;
}

// ==========================================
// [SECTION 3] MQTT SEND STATUS
// ==========================================
void sendLightStatus(String status) {
  if (!client.connected()) return;
  client.publish(topic_light_status, status.c_str(), true);
  Serial.print("📡 [MQTT] Sent Status: ");
  Serial.println(status);
}

// ==========================================
// [SECTION 4] CONTROL LOGIC
// ==========================================
void controlLight(bool turnOn) {
  Serial.println("\n--------------------------------");
  
  if (turnOn) {
    Serial.println(">> [ACTION] Command: Turn ON");
    digitalWrite(light, HIGH);
    lightState = HIGH;

    sendLightStatus("1"); // ส่งสถานะทันทีเพื่อให้ UI ตอบสนองไว

    delay(100); // รอไฟวิ่ง

    // Feedback Protection Logic
    if (ENABLE_FEEDBACK_PROTECTION) {
        // ใช้ฟังก์ชันที่เราเพิ่งเติมกลับมา
        if (isLightReallyOn()) {
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
        Serial.println("⚠️ [INFO] Feedback Protection is DISABLED (Test Mode)");
    }

  } else {
    Serial.println(">> [ACTION] Command: Turn OFF");
    digitalWrite(light, LOW);
    lightState = LOW;
    sendLightStatus("0");
  }
  Serial.println("--------------------------------\n");
}

// ==========================================
// [SECTION 5] MQTT CALLBACK
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
// [SECTION 6] SENSOR READING
// ==========================================
void readSensor() {
  if (sht30.read()) {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();

    char msgBuffer[64];
    snprintf(msgBuffer, sizeof(msgBuffer), "{\"temp\":%.1f,\"humi\":%.1f}", temperature, humidity);
    
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
// [SECTION 7] MQTT RECONNECT
// ==========================================
void reconnect() {
  while (!client.connected()) {
    if(WiFi.status() != WL_CONNECTED) return;

    Serial.print(">> [MQTT] Connecting to Broker...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass, "sensor/connection", 0, true, "OFFLINE")) {
      Serial.println(" Connected! ✅");
      
      digitalWrite(led, HIGH); 
      client.publish("sensor/connection", "ONLINE", true);
      
      client.subscribe("api/control");
      Serial.println(">> [SUB] Subscribed to 'api/control'");
      
      if (lightState == HIGH) sendLightStatus("1");
      else sendLightStatus("0");

    } else {
      Serial.print(" Failed (rc=");
      Serial.print(client.state());
      Serial.println(") Try again...");
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
  Serial.println("   (FIXED VERSION)");
  Serial.println("=================================");
  
  ledStart(led, 2, 250); 
  delay(1000);
  
  Wire.begin();
  if (sht30.begin()) Serial.println(">> [INIT] SHT30 Sensor: OK");
  else Serial.println("❌ [INIT] SHT30 Sensor: NOT FOUND");

  setup_wifi_manager(); 

  // ตั้งค่า MQTT
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);
  
  Serial.println(">> [INIT] System Ready.\n");
}

void loop() {
  // 1. Check WiFi
  if (WiFi.status() != WL_CONNECTED) {
     Serial.println("⚠️ [WiFi] Lost Connection! Restarting...");
     delay(3000);
     ESP.restart(); 
  }

  // 2. MQTT Loop
  if (!client.connected()) reconnect();
  client.loop();

  // 3. Manual Switch Logic
  int reading = digitalRead(switchPin);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("\n🔘 [MANUAL] Button Pressed");
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

  // 5. Safety Watchdog (Check only if Protection is Enabled)
  if (ENABLE_FEEDBACK_PROTECTION) {
      if (currentMillis - lastLightCheck >= lightCheckInterval) {
        lastLightCheck = currentMillis;
        if (lightState == HIGH) {
          // ✅ ตอนนี้ฟังก์ชันนี้มีอยู่จริงแล้ว ไม่แดงแล้วครับ
          if (!isLightReallyOn()) {
            Serial.println("\n🚨 [WATCHDOG] Critical Failure! Lost Load.");
            digitalWrite(light, LOW);
            lightState = LOW;
            sendLightStatus("0");
            client.publish("sensor/error", "TRIP: Watchdog Cut", false);
          }
        }
      }
  }
}