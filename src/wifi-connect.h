#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <WiFi.h>
#include <WiFiManager.h> 
#include <WebServer.h>
#include <DNSServer.h> 

// ⚙️ CONFIG: กำหนดขา LED แสดงสถานะ WiFi ที่นี่เลย (ไม่ต้องพึ่ง main.cpp)
#define WIFI_STATUS_LED 2  // LED บนบอร์ด ESP32 ปกติคือขา 2

// ---------------------------------------------------------
// Function: เรียกเมื่อหา WiFi ไม่เจอ และเข้าสู่โหมด Config
// ---------------------------------------------------------
inline void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("\n⭕ [WiFi Manager] Entered Config Mode");
  Serial.println(">> Please connect to Hotspot: " + myWiFiManager->getConfigPortalSSID());
  Serial.println(">> To configure WiFi Settings");
  
  // สั่งไฟติดค้าง เพื่อบอก User ว่ารอการ Config
  pinMode(WIFI_STATUS_LED, OUTPUT);
  digitalWrite(WIFI_STATUS_LED, HIGH); 
}

// ---------------------------------------------------------
// Function: Setup WiFi Manager (เรียกใน setup() ของ main)
// ---------------------------------------------------------
inline void setup_wifi_manager() {
  // ตั้งค่า LED เป็น Output ก่อน
  pinMode(WIFI_STATUS_LED, OUTPUT);
  
  WiFiManager wm;

  // 1. ตั้งค่า Debug (ช่วยดู Log ตอนเชื่อมต่อ)
  wm.setDebugOutput(true);

  // 2. ตั้ง Callback เมื่อเข้าโหมด Config
  wm.setAPCallback(configModeCallback);
  
  // 3. บังคับเปิด Captive Portal (เด้งหน้า Login อัตโนมัติ)
  wm.setCaptivePortalEnable(true);

  // 4. Timeout 3 นาที (180 วินาที) ถ้า User ไม่ Config จะปล่อยผ่านหรือ Reset
  wm.setConfigPortalTimeout(180); 

  // ⚠️ ล้างค่าเก่าเฉพาะตอนเทส (ถ้าใช้งานจริงให้ Comment บรรทัดนี้ทิ้ง)
  // wm.resetSettings(); 

  Serial.println(">> [WiFi Manager] Connecting...");
  
  // กระพริบไฟถี่ๆ เพื่อบอกว่ากำลังพยายามเชื่อมต่อ
  for(int i=0; i<5; i++) {
    digitalWrite(WIFI_STATUS_LED, HIGH); delay(100);
    digitalWrite(WIFI_STATUS_LED, LOW); delay(100);
  }

  // เริ่มการเชื่อมต่อ
  // ชื่อ AP: "SmartFarm-Config", รหัส: "password1234"
  if(!wm.autoConnect("Smartfarm-RUTS", "aie_112233")) {
    Serial.println("❌ [WiFi Manager] Failed to connect");
    
    // กรณี Timeout (ครบ 3 นาทีแล้วยังต่อไม่ได้) -> Restart เริ่มใหม่
    digitalWrite(WIFI_STATUS_LED, LOW); 
    delay(1000);
    ESP.restart(); 
  } 

  // ถ้าหลุดมาถึงตรงนี้ แสดงว่าต่อ WiFi สำเร็จแล้ว
  Serial.println("\n✅ [WiFi Manager] Connected!");
  Serial.print(">> IP Address: ");
  Serial.println(WiFi.localIP());
  
  // ดับไฟ LED เมื่อต่อเน็ตได้แล้ว (หรือจะให้ติดค้างก็ได้แล้วแต่ชอบ)
  digitalWrite(WIFI_STATUS_LED, LOW); 
}

#endif