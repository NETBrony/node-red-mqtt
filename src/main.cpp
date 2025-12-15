#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h" 

#define led 2           
#define light 19        
#define switchPin 17    
#define feedbackPin 34  

const int delay_ms = 500; 

// ⚡ แก้ไข: ส่งข้อมูลถี่ขึ้น (ทุก 2 วินาที) เพื่อให้ Heartbeat ไว
const unsigned long interval_sensor = 2000;  
const unsigned long lightCheckInterval = 100; 

unsigned long previousMillis = 0;
unsigned long lastLightCheck = 0;

#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);
float temperature = 0.0;
float humidity = 0.0;

int lightState = LOW;
int buttonState;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

WiFiClientSecure espClient; 
PubSubClient client(espClient);

void ledStart(int pin, int times, int speed) {
  for (int i = 0; i < times; i++) {
    digitalWrite(led, HIGH); delay(speed);
    digitalWrite(led, LOW); delay(speed);
  }
}

void ledStandby() {
  digitalWrite(led, HIGH); delay(delay_ms);
  digitalWrite(led, LOW); delay(delay_ms);
}

void mqttPending() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(led, HIGH); delay(100);
    digitalWrite(led, LOW); delay(100);
  }
  delay(2000); 
}

bool isLightReallyOn() {
  int sensorValue = analogRead(feedbackPin);
  if (sensorValue > 1000) return true; 
  return false;
}

void sendLightStatus(String status) {
  if (!client.connected()) return;
  client.publish("sensor/light_status", status.c_str(), true);
  Serial.print("   >> [MQTT SEND] Light Status: ");
  Serial.println(status);
}

void controlLight(bool turnOn) {
  Serial.println("\n--------------------------------");
  if (turnOn) {
    Serial.println(">> [ACTION] Attempting to turn ON...");
    digitalWrite(light, HIGH);
    lightState = HIGH;

    delay(500); 

    int val = analogRead(feedbackPin);
    Serial.print("   >> [CHECK] Feedback Value: ");
    Serial.println(val);

    if (val > 1000) {
      Serial.println("✅ [SUCCESS] Load Detected. System Normal.");
      sendLightStatus("1");
      client.publish("sensor/error", "OK", true);
    } else {
      Serial.println("🚨 [FAILURE] No Load Detected! (TRIP ACTIVATED)");
      Serial.println("   >> [SAFETY] Cutting Power immediately.");
      digitalWrite(light, LOW);
      lightState = LOW;
      sendLightStatus("0");
      client.publish("sensor/error", "PUMP ERROR", true);
    }
  } else {
    Serial.println(">> [ACTION] Turning OFF");
    digitalWrite(light, LOW);
    lightState = LOW;
    sendLightStatus("0");
  }
  Serial.println("--------------------------------\n");
}

void callback(char *topic, byte *payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) message += (char)payload[i];
  Serial.print("\n📩 [MQTT RECV] Topic: "); Serial.print(topic); Serial.print(" | Payload: "); Serial.println(message);
  if (String(topic) == "api/control") {
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
    Serial.print("🌡️ [SENSOR] Updated: "); Serial.println(msgBuffer);
    if (lightState == HIGH) sendLightStatus("1");
    else sendLightStatus("0");
  } else {
    Serial.println("❌ [ERROR] SHT30 Read Failed!");
  }
}

void setup_wifi() {
  delay(10);
  Serial.println(); Serial.print(">> [WiFi] Connecting to: "); Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { ledStandby(); Serial.print("."); }
  Serial.println("\n✅ [WiFi] Connected!"); Serial.print("   >> IP Address: "); Serial.println(WiFi.localIP());
  digitalWrite(led, HIGH); 
}

void reconnect() {
  while (!client.connected()) {
    Serial.print(">> [MQTT] Connecting to Broker...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass, "sensor/connection", 0, true, "OFFLINE")) {
      Serial.println(" Connected! ✅");
      digitalWrite(led, HIGH);
      client.publish("sensor/connection", "ONLINE", true);
      Serial.println("   >> [LWT] Status sent: ONLINE");
      client.subscribe("api/control");
      Serial.println("   >> [SUB] Subscribed to 'api/control'");
      if (lightState == HIGH) sendLightStatus("1");
      else sendLightStatus("0");
    } else {
      Serial.print(" Failed (rc="); Serial.print(client.state()); Serial.println(") Try again...");
      mqttPending(); 
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(led, OUTPUT); pinMode(light, OUTPUT); pinMode(feedbackPin, INPUT); pinMode(switchPin, INPUT_PULLUP);
  Serial.println("\n\n=================================");
  Serial.println("   SMART FARM PRO - SYSTEM START");
  Serial.println("=================================");
  ledStart(led, 2, 250); delay(1000);
  Wire.begin();
  if (sht30.begin()) Serial.println(">> [INIT] SHT30 Sensor: OK");
  else Serial.println("❌ [INIT] SHT30 Sensor: NOT FOUND");
  setup_wifi();
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(512);
  Serial.println(">> [INIT] System Ready. Waiting for commands...\n");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) setup_wifi();
  if (!client.connected()) reconnect();
  client.loop();

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

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval_sensor) {
    previousMillis = currentMillis;
    readSensor();
  }

  if (currentMillis - lastLightCheck >= lightCheckInterval) {
    lastLightCheck = currentMillis;
    if (lightState == HIGH) {
      if (!isLightReallyOn()) {
        Serial.println("\n🚨 [WATCHDOG] Critical Failure! Current lost during operation.");
        digitalWrite(light, LOW); lightState = LOW;
        sendLightStatus("0");
        client.publish("sensor/error", "PUMP ERROR", true);
      }
    }
  }
}