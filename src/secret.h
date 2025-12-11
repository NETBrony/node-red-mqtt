#ifndef SECRET_H
#define SECRET_H

char ssid[] = "@ Capsule Office";
char pass[] = "pJoeInwOnlyFan";

const char* mqtt_server = "192.168.5.168";
const int   mqtt_port   = 1883;
const char* mqtt_user   = "netbrony";
const char* mqtt_pass   = "Net_112233";

const char* topic_TempHumi     = "sensor/TempHumi";
const char* topic_light        = "test/light";
const char* topic_light_status = "sensor/light_status";

#endif