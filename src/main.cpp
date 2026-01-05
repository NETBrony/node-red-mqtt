#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h"

// ประกาศ Prototype ไว้ก่อน เพื่อให้ wifi-connect.h เรียกใช้ได้ (ถ้าจำเป็น)
void ledStandby();

#include "wifi-connect.h"

// ==========================================
// ⚙️ CONFIGURATION
// ==========================================
const bool ENABLE_FEEDBACK_PROTECTION = false; 

int led = 2;            
#define light 19        
#define switchPin 17    
#define feedbackPin 34  

const int delay_ms = 500; 
const unsigned long interval_sensor = 10000; 
const unsigned long lightCheckInterval = 100; 

// ==========================================
// VARIABLES
// ==========================================
unsigned long previousMillis = 0;
unsigned long lastLightCheck = 0;

// SHT31
#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);
float temperature = 0.0;
float humidity = 0.0;

// State
int lightState = LOW;
int buttonState;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// Network
WiFiClientSecure espClient; 
PubSubClient client(espClient);

// ==========================================
// [SECTION 1] UTILITY FUNCTIONS
// ==========================================

// ✅✅✅ เพิ่มฟังก์ชันนี้กลับมา เพื่อแก้ Error undefined reference
void ledStandby() {
  digitalWrite(led, HIGH);
  delay(delay_ms);
  digitalWrite(led, LOW);
  delay(delay_ms);
}

void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH); delay(speed);
    digitalWrite(led, LOW); delay(speed);
  }
}

void mqttPending() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(led, HIGH); delay(100);
    digitalWrite(led, LOW); delay(100);
  }
}

// ==========================================
// [SECTION 2] FEEDBACK CHECK
// ==========================================
bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  return (sensorValue > 1000); 
}

// ==========================================
// [SECTION 3] SEND STATUS
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
    sendLightStatus("1"); 

    delay(100); 

    if (ENABLE_FEEDBACK_PROTECTION) {
        if (isLightReallyOn()) {
          Serial.println("✅ [SUCCESS] Load Detected.");
          sendLightStatus("1");
        } else {
          Serial.println("🚨 [FAILURE] No Load Detected! (TRIP)");
          digitalWrite(light, LOW);
          lightState = LOW;
          sendLightStatus("0");
          client.publish(topic_error, "TRIP: Wire Broken/No Load", false);
        }
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

  if (String(topic) == topic_control) {
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

    sendLightStatus(lightState ? "1" : "0");

  } else {
    Serial.println("❌ [ERROR] SHT30 Read Failed!");
  }
}

// ==========================================
// [SECTION 7] RECONNECT
// ==========================================
void reconnect() {
  while (!client.connected()) {
    if(WiFi.status() != WL_CONNECTED) {
        Serial.println("Wait for WiFi...");
        delay(500);
        return;
    }

    // --- Offline Manual Control ---
    int reading = digitalRead(switchPin);
    if (reading == LOW) {
        while(digitalRead(switchPin) == LOW) { delay(10); } 
        lightState = !lightState;
        digitalWrite(light, lightState ? HIGH : LOW);
        Serial.print("🔘 [OFFLINE] Manual Toggle: ");
        Serial.println(lightState ? "ON" : "OFF");
    }
    // -----------------------------

    Serial.print(">> [MQTT] Connecting...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" Connected! ✅");
      
      digitalWrite(led, HIGH); 
      
      client.subscribe(topic_control);
      client.subscribe(topic_standby);
      
      Serial.println(">> [SUB] Subscribed Topics");
      sendLightStatus(lightState ? "1" : "0");

    } else {
      Serial.print(" Failed (rc=");
      Serial.print(client.state());
      Serial.println(") Try again...");
      delay(2000); 
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

  Serial.println("\n\n=== SMART FARM PRO START ===");
  ledStart(led, 2, 250); 
  delay(1000);
  
  Wire.begin();
  if (sht30.begin()) Serial.println(">> [INIT] SHT30: OK");
  else Serial.println("❌ [INIT] SHT30: FAIL");

  setup_wifi_manager(); 

  // MQTT Setup
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) ESP.restart();
  if (!client.connected()) reconnect();
  client.loop();

  // --- Manual Switch Logic ---
  int reading = digitalRead(switchPin);
  if (reading != lastButtonState) { lastDebounceTime = millis(); }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        Serial.println("\n🔘 [MANUAL] Button Pressed");
        bool newState = !lightState;
        controlLight(newState); 
        sendLightStatus(newState ? "1" : "0");
      }
    }
  }
  lastButtonState = reading;

  // --- Sensor Loop ---
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval_sensor) {
    previousMillis = currentMillis;
    readSensor();
  }

  // --- Watchdog ---
  if (ENABLE_FEEDBACK_PROTECTION) {
      if (currentMillis - lastLightCheck >= lightCheckInterval) {
        lastLightCheck = currentMillis;
        if (lightState == HIGH && !isLightReallyOn()) {
            Serial.println("🚨 [WATCHDOG] Cut Power!");
            digitalWrite(light, LOW);
            lightState = LOW;
            sendLightStatus("0");
            client.publish(topic_error, "TRIP: Watchdog", false);
        }
      }
  }
}