#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h" 

// ==========================================
// 1. PIN & CONFIG DEFINITIONS
// ==========================================
const int led = 2;             // LED บนบอร์ด
const int light = 19;          // ขารีเลย์คุมไฟ
const int switchPin = 17;      // สวิตช์ปุ่มกด
const int feedbackPin = 34;    // เช็คกระแส (Analog)

const int delay_ms = 500;
const unsigned long interval_sensor = 10000;
const unsigned long lightCheckInterval = 1000;
const unsigned long debounceDelay = 50;

// ==========================================
// 2. GLOBAL VARIABLES
// ==========================================
unsigned long previousMillis = 0;
unsigned long lastLightCheck = 0;

// SHT30
#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);
float temperature = 0.0;
float humidity = 0.0;

// Status Variables
int lightState = LOW;
int buttonState;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;

// Network
WiFiClientSecure espClient; 
PubSubClient client(espClient);

// ==========================================
// 3. HELPER FUNCTIONS
// ==========================================

// ฟังก์ชันกระพริบไฟและรอ WiFi (แก้ไข Loop ให้วนในตัว)
void ledStandby()
{
  // วนลูปจนกว่า WiFi จะต่อติด (WL_CONNECTED)
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(led, HIGH);
    delay(delay_ms);
    if (WiFi.status() == WL_CONNECTED) break; // เช็คจุดที่ 1

    digitalWrite(led, LOW);
    delay(delay_ms);
    if (WiFi.status() == WL_CONNECTED) break; // เช็คจุดที่ 2

    Serial.print("."); // คงการปริ้นท์จุดไว้ตามเดิม
  }
}

// ฟังก์ชันเช็คกระแส (Low-side Sensing)
bool isLightReallyOn()
{
  int sensorValue = analogRead(feedbackPin);
  // Serial.print("Feedback Value: "); Serial.println(sensorValue); // คง comment ไว้ตามต้นฉบับ
  if (sensorValue > 1000)
  {
    return true;
  }
  else
  {
    return false;
  }
}

// ฟังก์ชันส่งสถานะพร้อม Retain (คืนค่า Log เดิม)
void sendLightStatus(String status)
{
  if (!client.connected()) return;

  if (status == "1")
  {
    client.publish("sensor/light_status", "1", true);
    client.publish("sensor/error", "OK", true);
  }
  else
  {
    client.publish("sensor/light_status", "0", true);
  }

  client.loop();
  // คืนค่า Log เดิม
  Serial.println(">> MQTT Status Sent: " + status + " (Retained)");
}

// ฟังก์ชันควบคุมไฟ (คืนค่า Log เดิม)
void controlLight(bool turnOn)
{
  if (turnOn)
  {
    digitalWrite(light, HIGH);
    lightState = HIGH;

    delay(500); // รอไฟเดิน

    if (isLightReallyOn())
    {
      Serial.println("Action: ON Success"); // Log เดิม
      sendLightStatus("1");
    }
    else
    {
      Serial.println("Action: ON Failed (Bulb Broken)"); // Log เดิม
      digitalWrite(light, LOW);
      lightState = LOW;
      sendLightStatus("0");
      client.publish("sensor/error", "Start Fail", true);
      client.loop();
    }
  }
  else
  {
    digitalWrite(light, LOW);
    lightState = LOW;
    Serial.println("Action: OFF"); // Log เดิม
    sendLightStatus("0");
  }
}

// ฟังก์ชันตรวจสอบสุขภาพหลอดไฟ
void monitorLightHealth()
{
  if (lightState == HIGH)
  {
    if (!isLightReallyOn())
    {
      Serial.println("ALERT: Light failure detected during operation!"); // Log เดิม
      digitalWrite(light, LOW);
      lightState = LOW;

      sendLightStatus("0");
      client.publish("sensor/error", "Bulb Broken!", true);
      client.loop();
    }
  }
}

void readSensor()
{
  if (sht30.read())
  {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();

    char msgBuffer[64];
    snprintf(msgBuffer, sizeof(msgBuffer), "{\"temp\":%.1f,\"humi\":%.1f}", temperature, humidity);
    client.publish("sensor/TempHumi", msgBuffer);

    if (lightState == HIGH)
      sendLightStatus("1");
    else
      sendLightStatus("0");

    // คืนค่า Log เดิม
    Serial.print("Update Sensor & Sync Light: ");
    Serial.println(msgBuffer);
  }
  else
  {
    Serial.println("Error: Can't read SHT30 sensor!"); // Log เดิม
  }
}

// ฟังก์ชันไฟกระพริบตอนเริ่มระบบ (เก็บไว้ตามเดิม)
void ledStart(int pin, int times, int speed)
{
  for (int i = 0; i < times; i++)
  {
    digitalWrite(led, HIGH);
    delay(speed);
    digitalWrite(led, LOW);
    delay(speed);
  }
}

// ฟังก์ชันไฟกระพริบรอ MQTT
void mqttPending()
{
  for (int i = 0; i < 2; i++)
  {
    digitalWrite(led, HIGH);
    delay(100);
    digitalWrite(led, LOW);
    delay(100);
  }
  delay(2000); 
}

// ==========================================
// 4. MAIN FUNCTIONS (Setup & Loop)
// ==========================================

void setup_wifi()
{
  delay(10);
  Serial.print("\n[WiFi] Connecting to: "); // Log เดิม
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  // เรียกใช้ ledStandby แบบใหม่ (ที่วนลูปในตัว)
  ledStandby(); 

  Serial.println("\nWiFi Connected!"); // Log เดิม
  digitalWrite(led, HIGH);
}

void callback(char *topic, byte *payload, unsigned int length)
{
  String message = "";
  for (int i = 0; i < length; i++)
    message += (char)payload[i];
    
  // คืนค่า Log เดิม
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (String(topic) == "api/control") 
  {
    if (message == "1") {
      Serial.println("Command from App: ON"); // Log เดิม
      controlLight(true); 
    } 
    else if (message == "0") {
      Serial.println("Command from App: OFF"); // Log เดิม
      controlLight(false);
    }
  }
}

void reconnect()
{
  while (!client.connected())
  {
    mqttPending();
    Serial.print("Attempting MQTT connection..."); // Log เดิม
    String clientId = "ESP32Client-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass))
    {
      Serial.println("connected"); // Log เดิม
      digitalWrite(led, HIGH);

      client.subscribe("api/control"); 

      if (lightState == HIGH) sendLightStatus("1");
      else sendLightStatus("0");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds"); // Log เดิม
      mqttPending(); 
    }
  }
}

void switchPush()
{
  int reading = digitalRead(switchPin);
  if (reading != lastButtonState)
    lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == LOW)
      {
        Serial.println("\n[Button] Manual Toggle"); // Log เดิม
        controlLight(!lightState); // ใช้ !lightState สลับ HIGH/LOW ได้เลย (เพราะ HIGH=1, LOW=0)
      }
    }
  }
  lastButtonState = reading;
}

void setup()
{
  Serial.begin(115200);
  delay(100);

  pinMode(led, OUTPUT);
  pinMode(light, OUTPUT);
  pinMode(feedbackPin, INPUT);
  pinMode(switchPin, INPUT_PULLUP);

  Serial.println("\nStarting System..."); // Log เดิม
  ledStart(led, 2, 250);
  delay(2000);

  Wire.begin();
  if (!sht30.begin())
  {
    Serial.println("Can't find SHT30 sensor!"); // Log เดิม
  }
  else
  {
    Serial.println("SHT30 Connected."); // Log เดิม
  }

  setup_wifi();

  espClient.setInsecure(); 
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512); 
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
    setup_wifi();
  if (!client.connected())
    reconnect();
  client.loop();

  switchPush();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval_sensor)
  {
    previousMillis = currentMillis;
    readSensor();
  }

  if (currentMillis - lastLightCheck >= lightCheckInterval)
  {
    lastLightCheck = currentMillis;
    monitorLightHealth();
  }
}
