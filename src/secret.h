#ifndef SECRET_H
#define SECRET_H

char ssid[] = "@ Capsule Office";
char pass[] = "pJoeInwOnlyFan";
// char ssid[] = "NETBrony_2.4G";
// char pass[] = "Net_3343254622";

const char* mqtt_server = "e53e2233d1a141699f1204a648c861c2.s1.eu.hivemq.cloud";
const int   mqtt_port   = 8883;
const char* mqtt_user   = "netbrony";
const char* mqtt_pass   = "Net_112233";

const char* topic_TempHumi     = "sensor/TempHumi";
const char* topic_light        = "test/light";
const char* topic_light_status = "sensor/light_status";

#endif