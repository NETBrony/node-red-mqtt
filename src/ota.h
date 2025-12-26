#ifndef OTA_H
#define OTA_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <PubSubClient.h>
#include "secret.h"

extern PubSubClient client; 

inline void update_progress(int cur, int total) {
    static int lastPercent = -1;
    int percent = (cur * 100) / total;
    
    if (percent != lastPercent && percent % 10 == 0) {
        char msg[16];
        snprintf(msg, sizeof(msg), "%d", percent);
        
        if (client.connected()) {
            // ✅ ใช้ตัวแปรแทน
            client.publish(topic_ota_progress, msg);
        }
        Serial.printf("⬇️ OTA Progress: %d%%\n", percent);
        lastPercent = percent;
    }
}

inline void runOTA(String firmwareURL) {
    Serial.println("\n=================================");
    Serial.println("🚀 STARTING OTA UPDATE...");
    Serial.println("🌍 Downloading from: " + firmwareURL);
    Serial.println("=================================");

    // ✅ ใช้ตัวแปรแทน
    if (client.connected()) client.publish(topic_ota_status, "STARTING");

    WiFiClient wifiClient;
    
    httpUpdate.setLedPin(2, LOW); 
    httpUpdate.rebootOnUpdate(true); 

    Update.onProgress(update_progress);

    t_httpUpdate_return ret = httpUpdate.update(wifiClient, firmwareURL);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("❌ OTA Failed Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            if (client.connected()) {
                // ✅ ใช้ตัวแปรแทน
                client.publish(topic_ota_status, "FAILED");
                client.publish(topic_error, httpUpdate.getLastErrorString().c_str());
            }
            break;

        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("⚠️ OTA No Updates");
            if (client.connected()) client.publish(topic_ota_status, "NO_UPDATE");
            break;

        case HTTP_UPDATE_OK:
            Serial.println("✅ OTA Completed! Rebooting...");
            if (client.connected()) client.publish(topic_ota_status, "COMPLETED");
            break;
    }
}

#endif