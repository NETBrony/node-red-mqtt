#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <WiFi.h>
#include <WiFiManager.h> 
#include <WebServer.h>

// ประกาศตัวแปร extern
extern int led; 
extern void ledStandby(); 

// 2. เติม inline เพื่อป้องกัน Error หากโปรเจกต์ขยายตัว
inline void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("\n⭕ [WiFi Manager] Entered Config Mode");
  Serial.println(">> Please connect to Hotspot: " + myWiFiManager->getConfigPortalSSID());
  Serial.println(">> To configure WiFi Settings");
  
  // 3. เปลี่ยนจาก ledStandby() เป็นไฟติดค้าง เพื่อให้ User รู้ว่าต้องทำรายการ
  ledStandby(); 
}

inline void setup_wifi_manager() {
  WiFiManager wm;

  // set callback
  wm.setAPCallback(configModeCallback);
  
  // Timeout 3 นาที
  wm.setConfigPortalTimeout(180); 

  Serial.println(">> [WiFi Manager] Connecting...");
  
  // ถ้าต่อไม่ได้ ให้ปล่อย Hotspot
  if(!wm.autoConnect("SmartFarm_Config", "password1234")) {
    Serial.println("❌ [WiFi Manager] Failed to connect");
    
    // กระพริบเตือนก่อนรีเซ็ต 
    // (ตรงนี้ใช้ ledStandby ได้ เพราะกำลังจะตายแล้ว)
    ledStandby(); 
    ledStandby();
    
    ESP.restart(); 
  } 

  Serial.println("\n✅ [WiFi Manager] Connected!");
  Serial.print(">> IP Address: ");
  Serial.println(WiFi.localIP());
}

#endif