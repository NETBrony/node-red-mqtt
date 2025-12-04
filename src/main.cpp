#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h"

#define led 2
#define light 19
#define switchPin 17
#define feedbackPin 34 

const int delay_ms = 500;
const unsigned long interval_sensor = 10000;
unsigned long previousMillis = 0;

// === [ส่วนที่เพิ่ม] ตัวแปรสำหรับเช็คหลอดไฟตลอดเวลา ===
unsigned long lastLightCheck = 0;
const unsigned long lightCheckInterval = 1000; // เช็คทุกๆ 1 วินาที

//===== SHT30 =====
#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);
float temperature = 0.0;
float humidity = 0.0;

//===== Switch Status =========
int lightState = LOW;
int buttonState;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

WiFiClient espClient;
PubSubClient client(espClient);

// ฟังก์ชันเช็คกระแส (เหมือนเดิม)
bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  // Serial.print("[Monitor] Sensor: "); 
  // Serial.println(sensorValue); // เปิดคอมเมนต์ถ้าอยากดูค่ารัวๆ
  if (sensorValue > 30) { 
    return true; 
  } else {
    return false;
  }
}

// ฟังก์ชันส่งสถานะ (แยกออกมาเพื่อเรียกใช้ได้หลายที่)
void sendLightStatus(String status) {
  if (status == "1") {
    client.publish("sensor/light_status", "1");
    client.publish("sensor/error", "OK");
  } else {
    client.publish("sensor/light_status", "0");
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.println(message);

  if (String(topic) == "test/light") {
    if (message == "1") {
      digitalWrite(light, HIGH);
      lightState = HIGH;
      delay(100); 
      
      // เช็คครั้งแรกตอนสั่งเปิด
      if (isLightReallyOn()) {
        Serial.println("Command ON: Success");
        sendLightStatus("1");
      } else {
        Serial.println("Command ON: Failed (Bulb Broken)");
        digitalWrite(light, LOW);
        lightState = LOW;
        sendLightStatus("0");
        client.publish("sensor/error", "Start Fail");
      }
    }
    else if (message == "0") {
      digitalWrite(light, LOW);
      lightState = LOW;
      Serial.println("Command OFF");
      sendLightStatus("0");
    }
  }
}

void readSensor() {
  if (sht30.read()) {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();
    String jsonString = "{\"temp\":" + String(temperature) + ",\"humi\":" + String(humidity) + "}";
    char msgBuffer[100];
    jsonString.toCharArray(msgBuffer, 100);
    client.publish("sensor/TempHumi", msgBuffer);
    Serial.println(msgBuffer);
  }
}

void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH); delay(speed); digitalWrite(led, LOW); delay(speed);
  }
}

void ledStandby() {
  digitalWrite(led, HIGH); delay(delay_ms); digitalWrite(led, LOW); delay(delay_ms);
}

void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); ledStandby();
  }
  digitalWrite(led, HIGH);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      client.subscribe(topic_light); 
    } else {
      delay(5000);
    }
  }
}

void switchPush() {
  int reading = digitalRead(switchPin);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        // Toggle
        lightState = !lightState;
        digitalWrite(light, lightState);
        
        if (lightState == HIGH) {
           delay(100);
           if (isLightReallyOn()) {
             Serial.println("Button ON: Success");
             sendLightStatus("1");
           } else {
             Serial.println("Button ON: Failed");
             digitalWrite(light, LOW);
             lightState = LOW;
             sendLightStatus("0");
           }
        } else {
           Serial.println("Button OFF");
           sendLightStatus("0");
        }
      }
    }
  }
  lastButtonState = reading;
}

// === [ส่วนสำคัญ] ฟังก์ชันเฝ้าระวัง ===
void monitorLightHealth() {
  // ทำงานเฉพาะตอนที่ระบบ "คิดว่าไฟเปิดอยู่" (lightState == HIGH)
  if (lightState == HIGH) {
    // ถ้าตรวจแล้วพบว่า "ไฟดับจริง" (isLightReallyOn == false)
    if (!isLightReallyOn()) {
      Serial.println("ALERT: Light failure detected during operation!");
      
      // 1. สั่งตัดระบบ Software ทันที
      lightState = LOW;
      digitalWrite(light, LOW);
      
      // 2. แจ้งเตือนไปที่ Node-RED
      sendLightStatus("0");
      client.publish("sensor/error", "Lost Connection"); // แจ้งว่าสายหลุด
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(led, OUTPUT);
  pinMode(light, OUTPUT);
  pinMode(feedbackPin, INPUT);
  pinMode(switchPin, INPUT_PULLUP);

  ledStart(led, 2, 250);
  Wire.begin();
  sht30.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) setup_wifi();
  if (!client.connected()) reconnect();
  client.loop();
  
  switchPush(); 

  unsigned long currentMillis = millis();
  
  // 1. รอบอ่าน Sensor (ทุก 10 วิ)
  if (currentMillis - previousMillis >= interval_sensor) {
    previousMillis = currentMillis;
    readSensor();
  }

  // 2. รอบตรวจสุขภาพหลอดไฟ (ทุก 1 วิ) [เพิ่มใหม่]
  if (currentMillis - lastLightCheck >= lightCheckInterval) {
    lastLightCheck = currentMillis;
    monitorLightHealth(); // เรียกยามมาตรวจ
  }
}
