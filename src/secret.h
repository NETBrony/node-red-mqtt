#ifndef SECRET_H
#define SECRET_H

const char* mqtt_server = "e53e2233d1a141699f1204a648c861c2.s1.eu.hivemq.cloud";
const int   mqtt_port   = 8883;
const char* mqtt_user   = "netbrony";
const char* mqtt_pass   = "Net_112233";

const char* topic_TempHumi     = "sensor/TempHumi";
const char* topic_light        = "test/light";
const char* topic_light_status = "sensor/light_status";
const char* topic_pump_log     = "status/pump";

const char* topic_ota_update    = "ota/update";
const char* topic_ota_status    = "ota/status";
const char* topic_ota_progress  = "ota/progress";
const char* topic_error         = "sensor/error";

#endif