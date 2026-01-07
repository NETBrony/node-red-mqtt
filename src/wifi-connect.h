#ifndef WIFI_CONNECT_H
#define WIFI_CONNECT_H

#include <WiFiManager.h> 
#include "esp_wifi.h" 

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("\n⭕ [WiFi Manager] Entered Config Mode");
  Serial.print(">> Please connect to Hotspot: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print(">> IP Address: ");
  Serial.println(WiFi.softAPIP());
}

void setup_wifi_manager() {
  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  
  WiFi.setSleep(false); 
  WiFi.setTxPower(WIFI_POWER_5dBm); // ลดกำลังส่งตอน Setup แก้ไฟกระชาก

  wm.setConnectTimeout(10); 
  wm.setConfigPortalTimeout(0); 

  const char* apName = "SmartFarm-Setup"; 
  
  Serial.println("🔄 Connecting to WiFi...");
  
  if(!wm.autoConnect(apName)) {
    Serial.println("❌ Failed to connect (Infinite AP Mode)");
    delay(3000);
    ESP.restart(); 
  } 
  
  WiFi.setTxPower(WIFI_POWER_13dBm); // คืนค่ากำลังส่งเมื่อใช้งานจริง

  Serial.println("✅ WiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

#endif