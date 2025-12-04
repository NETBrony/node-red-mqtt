#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <SHT31.h>
#include "secret.h"

#define led 2
#define light 19
#define switchPin 17

const int delay_ms = 500;
const int delay_short = 100;
const unsigned long interval_sensor = 10000;
unsigned long previousMillis = 0;

//===== SHT30 =====
#define SHT31_ADDRESS 0x44
SHT31 sht30(SHT31_ADDRESS);

float temperature = 0.0;
float humidity = 0.0;
//=================

//===== Switch Status =========
int lightState = LOW;         // สถานะไฟปัจจุบัน (LOW=ปิด, HIGH=เปิด)
int buttonState;             // สถานะปุ่มที่อ่านได้ปัจจุบัน
int lastButtonState = HIGH;  // สถานะปุ่มครั้งก่อนหน้า (เริ่มที่ HIGH เพราะ INPUT_PULLUP)
unsigned long lastDebounceTime = 0;  // เวลาล่าสุดที่สถานะปุ่มเปลี่ยน
unsigned long debounceDelay = 50;    // หน่วงเวลาแก้สั่น (50ms)
//=============================

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char *topic, byte *payload, unsigned int length)
{
  // 1. แสดง Debug ว่ามีข้อความเข้ามา
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  // 2. แปลง Payload เป็น String
  String message = "";
  for (int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }
  Serial.println(message);

  // 3. ตรวจสอบ Topic และสั่งงาน
  if (String(topic) == "test/light")
  {
    if (message == "1")
    {
      // สั่งเปิดไฟ
      digitalWrite(light, HIGH);
      Serial.println("Turn ON Light");

      // === [ส่วนที่เพิ่ม] ส่ง Feedback กลับไปบอก Node-RED ว่าเปิดแล้วนะ ===
      client.publish("sensor/light_status", "1");
    }
    else if (message == "0")
    {
      // สั่งปิดไฟ
      digitalWrite(light, LOW);
      Serial.println("Turn OFF Light");

      // === [ส่วนที่เพิ่ม] ส่ง Feedback กลับไปบอก Node-RED ว่าปิดแล้วนะ ===
      client.publish("sensor/light_status", "0");
    }
  }
}

void readSensor()
{

  if (sht30.read())
  {
    temperature = sht30.getTemperature();
    humidity = sht30.getHumidity();

    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");

    String jsonString = "{";
    jsonString += "\"temp\":";
    jsonString += String(temperature);
    jsonString += ",";
    jsonString += "\"humi\":";
    jsonString += String(humidity);
    jsonString += "}";

    // แปลง String เป็น char array เพื่อส่งผ่าน MQTT
    char msgBuffer[100];
    jsonString.toCharArray(msgBuffer, 100);

    // 3. ส่วนที่เพิ่ม: ส่งข้อมูล (Publish) ไปยัง Topic "sensor/temp"
    // ต้องตรงกับที่ตั้งไว้ใน Node-RED (mqtt in node)
    client.publish("sensor/TempHumi", msgBuffer);

    Serial.print("Published JSON: ");
    Serial.println(msgBuffer);
  }
  else
  {
    Serial.println("Can't read SHT30 sensor!");
  }
}

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

void ledStandby()
{
  digitalWrite(led, HIGH);
  delay(delay_ms);
  digitalWrite(led, LOW);
  delay(delay_ms);
}

void setup_wifi()
{
  delay(10);
  Serial.print("\n[WiFi] Connecting to: ");
  Serial.println(ssid);

  WiFi.begin(ssid, pass);

  // วนลูปจนกว่าจะต่อ WiFi ได้ (Blocking จนกว่าจะสำเร็จ)
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    ledStandby();
  }

  Serial.println("\nWiFi Connected! :]");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(led, HIGH);
}

void reconnect()
{
  // วนลูปตลอดไป จนกว่าจะต่อ MQTT ได้
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");

    // สร้าง Client ID (สุ่มเพื่อไม่ให้ซ้ำ)
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    // พยายามเชื่อมต่อ
    // ถ้า secret.h ไม่มี user/pass ให้ใช้ client.connect(clientId.c_str())
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass))
    {
      Serial.println("connected");
      client.subscribe(topic_light);
      client.subscribe(topic_TempHumi);
      Serial.println("Subscribed to: test/light");
      Serial.println("Subscribed to: sensor/TempHumi");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void switchPush(){
  int reading = digitalRead(switchPin);

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay) {
    // ถ้าสถานะเปลี่ยนไปจริงๆ
    if (reading != buttonState) {
      buttonState = reading;

      // ทำงานเฉพาะตอนกดลง (LOW) สำหรับ INPUT_PULLUP
      if (buttonState == LOW) {
        lightState = !lightState; // สลับสถานะ (Toggle)

        // สั่งงาน Hardware
        digitalWrite(light, lightState);
        
        // ส่งสถานะบอก Node-RED
        if (lightState == HIGH) {
          Serial.println("Turn ON Light (Button)");
          client.publish("sensor/light_status", "1");
        } else {
          Serial.println("Turn OFF Light (Button)");
          client.publish("sensor/light_status", "0");
        }
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
  pinMode(switchPin, INPUT_PULLUP);

  Serial.println("\nStarting System...");
  ledStart(led, 2, 250);
  delay(2000);

  Wire.begin();
  Wire.setClock(100000);

  if (!sht30.begin())
  {
    Serial.println("Can't find SHT30 sensor!");
  }
  else
  {
    Serial.println("SHT30 Connected! :]");
  }

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Disconnected! Try again :[");
    setup_wifi();
  }

  if (!client.connected())
  {
    reconnect();
  }

  client.loop();
  switchPush();

  client.loop();
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval_sensor)
  {
    previousMillis = currentMillis;
    readSensor();
  }
}