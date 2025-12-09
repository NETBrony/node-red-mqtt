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

// ตัวแปรสำหรับเช็คหลอดไฟตลอดเวลา
unsigned long lastLightCheck = 0;
const unsigned long lightCheckInterval = 1000; 

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

// ฟังก์ชันเช็คกระแส (Low-side Sensing)
bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  // Serial.print("Feedback Value: "); Serial.println(sensorValue); 
  if (sensorValue > 1000) { 
    return true; 
  } else {
    return false;
  }
}

// ฟังก์ชันส่งสถานะพร้อม Retain
void sendLightStatus(String status) {
  if (!client.connected()) return;

  if (status == "1") {
    client.publish("sensor/light_status", "1", true);
    client.publish("sensor/error", "OK", true);
  } else {
    client.publish("sensor/light_status", "0", true);
  }
  
  client.loop(); 
  Serial.println(">> MQTT Status Sent: " + status + " (Retained)");
}

// ฟังก์ชันควบคุมไฟ
void controlLight(bool turnOn) {
  if (turnOn) {
    digitalWrite(light, HIGH);
    lightState = HIGH;
    
    delay(500); // รอไฟเดิน

    if (isLightReallyOn()) {
      Serial.println("Action: ON Success");
      sendLightStatus("1");
    } else {
      Serial.println("Action: ON Failed (Bulb Broken)");
      digitalWrite(light, LOW);
      lightState = LOW;
      sendLightStatus("0");
      client.publish("sensor/error", "Start Fail", true);
      client.loop();
    }
  } else {
    digitalWrite(light, LOW);
    lightState = LOW;
    Serial.println("Action: OFF");
    sendLightStatus("0");
  }
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.print("Message arrived ["); Serial.print(topic); Serial.print("]: "); Serial.println(message);

  if (String(topic) == "test/light") {
    if (message == "1") controlLight(true);
    else if (message == "0") controlLight(false);
  }
}

void readSensor() {
  if (sht30.read()) {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();
    
    char msgBuffer[64];
    snprintf(msgBuffer, sizeof(msgBuffer), "{\"temp\":%.1f,\"humi\":%.1f}", temperature, humidity);
    client.publish("sensor/TempHumi", msgBuffer);
    
    if (lightState == HIGH) sendLightStatus("1");
    else sendLightStatus("0");

    Serial.print("Update Sensor & Sync Light: "); Serial.println(msgBuffer);
  } else {
    Serial.println("Error: Can't read SHT30 sensor!"); 
  }
}

// === [กู้คืน] ฟังก์ชันไฟกระพริบต่างๆ ===

// 1. ไฟกระพริบตอนเริ่มระบบ
void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH); delay(speed); digitalWrite(led, LOW); delay(speed);
  }
}

// 2. ไฟกระพริบรอ WiFi (Standby)
void ledStandby() {
  digitalWrite(led, HIGH); delay(delay_ms); digitalWrite(led, LOW); delay(delay_ms);
}

// 3. ไฟกระพริบรอ MQTT (Pending)
void mqttPending(){
  for(int i = 0; i < 2; i++){
    digitalWrite(led, HIGH); delay(100); digitalWrite(led, LOW); delay(100);
  }
  delay(2000); // เว้นจังหวะ 2 วินาที
}

void setup_wifi() {
  delay(10);
  Serial.print("\n[WiFi] Connecting to: "); Serial.println(ssid);

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    ledStandby(); // เรียกใช้ไฟกระพริบรอ WiFi
    Serial.print("."); 
  }
  Serial.println("\nWiFi Connected!"); 
  digitalWrite(led, HIGH);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection..."); 
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");  
      digitalWrite(led, HIGH);
      
      client.subscribe("test/light"); 
      
      if(lightState == HIGH) sendLightStatus("1");
      else sendLightStatus("0");
      
    } else {
      Serial.print("failed, rc="); Serial.print(client.state()); 
      Serial.println(" try again in 5 seconds"); 
      
      mqttPending(); // เรียกใช้ไฟกระพริบรอ MQTT
      // delay(5000); // ตัด delay ออกเพราะใน mqttPending มี delay 2 วิแล้ว (หรือจะใส่เพิ่มก็ได้ถ้าอยากรอนานขึ้น)
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
        Serial.println("\n[Button] Manual Toggle");
        controlLight(!lightState); 
      }
    }
  }
  lastButtonState = reading;
}

void monitorLightHealth() {
  if (lightState == HIGH) {
    if (!isLightReallyOn()) {
      Serial.println("ALERT: Light failure detected during operation!");
      digitalWrite(light, LOW);
      lightState = LOW;
      
      sendLightStatus("0");
      client.publish("sensor/error", "Bulb Broken!", true); 
      client.loop();
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

  Serial.println("\nStarting System..."); 
  ledStart(led, 2, 250); // ไฟกระพริบเริ่มระบบ
  delay(2000);

  Wire.begin();
  if(!sht30.begin()) {
     Serial.println("Can't find SHT30 sensor!");
  } else {
     Serial.println("SHT30 Connected.");
  }

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
  if (currentMillis - previousMillis >= interval_sensor) {
    previousMillis = currentMillis;
    readSensor();
  }

  if (currentMillis - lastLightCheck >= lightCheckInterval) {
    lastLightCheck = currentMillis;
    monitorLightHealth();
  }
}