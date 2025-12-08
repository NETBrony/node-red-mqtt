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
  // Serial.print("[Monitor] Sensor: "); Serial.println(sensorValue); 
  if (sensorValue > 30) { 
    return true; 
  } else {
    return false;
  }
}

// ฟังก์ชันส่งสถานะ MQTT
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
    Serial.print("Published SHT30: "); // คืนค่า Debug
    Serial.println(msgBuffer);
  } else {
    Serial.println("Error: Can't read SHT30 sensor!"); // คืนค่า Debug
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

void mqttPending(){
  for(int i = 0; i < 2; i++){
    digitalWrite(led, HIGH);
    delay(100);
    digitalWrite(led, LOW);
    delay(100);
  }
  delay(2000);
}

// === [กู้คืน] Debug Message WiFi ===
void setup_wifi() {
  delay(10);
  Serial.print("\n[WiFi] Connecting to: "); // คืนค่า
  Serial.println(ssid);                     // คืนค่า

  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    ledStandby();
    delay(500); 
    Serial.print("."); // คืนค่า (จุดไข่ปลา)
  }
  
  Serial.println("\nWiFi Connected! :]");   // คืนค่า
  Serial.print("IP address: ");             // คืนค่า
  Serial.println(WiFi.localIP());           // คืนค่า
  digitalWrite(led, HIGH);
}

// === [กู้คืน] Debug Message MQTT ===
void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection..."); // คืนค่า
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");  // คืนค่า
      digitalWrite(led, HIGH);
      client.subscribe(topic_light); 
      Serial.println("Subscribed to: test/light"); // คืนค่า
    } else {
      Serial.print("failed, rc=");  // คืนค่า
      Serial.print(client.state()); // คืนค่า
      Serial.println(" try again in 5 seconds"); // คืนค่า
      mqttPending();
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

// ฟังก์ชันเฝ้าระวังหลอดขาด (ทำงานตลอดเวลา)
void monitorLightHealth() {
  if (lightState == HIGH) {
    if (!isLightReallyOn()) {
      Serial.println("ALERT: Light failure detected during operation!");
      
      lightState = LOW;
      digitalWrite(light, LOW);
      
      sendLightStatus("0");
      client.publish("sensor/error", "Lost Connection"); 
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

  Serial.println("\nStarting System..."); // คืนค่า
  ledStart(led, 2, 250);
  delay(3000);

  Wire.begin();
  if (!sht30.begin()) {
    Serial.println("Can't find SHT30 sensor!"); // คืนค่า
  } else {
    Serial.println("SHT30 Connected! :]\n");      // คืนค่า
  }

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Disconnected! Reconnecting..."); // เพิ่มให้ด้วย
    setup_wifi();
  }
  
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