#ifndef MQTTMANAGER_H
#define MQTTMANAGER_H

void initMqtt();
// void mqttSubscribe(const char *subTopic, const int QoS);
void mqttPublish(const char* pubTpic, const char *message, const int QoS, const int retain, const bool store);

#endif