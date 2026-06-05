#include "config.h"
// #include "cmd.h" DEPRECIATED
#include "mqttManager.h"
#include "mqtt_client.h"

// Calling different root topic handler
#include "functions/cmnd/cmnd.h"
#include "functions/stat/stat.h"
#include "functions/tele/tele.h"
#include "functions/conf/conf.h"

#include "statusManager/statusManager.h"

esp_mqtt_client_handle_t client;

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

  switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED: {
      Serial.println("Connected to GridFlow EMQX Server");
      String onlineMessageTopic = "stat/" + username + "/onoff";
      esp_mqtt_client_enqueue(client, onlineMessageTopic.c_str(), "{\"status\": \"online\"}", 0, 2, 1, true);
      String cmndTopic = "cmnd/" + username + "/#";
      esp_mqtt_client_subscribe(client, cmndTopic.c_str(), 2);
      String confTopic = "conf/" + username + "/#";
      esp_mqtt_client_subscribe(client, confTopic.c_str(), 2);
      esp_mqtt_client_subscribe(client, "cmnd/all/#", 2);
      globalErrorCounter = 0;
      statusHandler(STATE_ALL_IS_WELL);
      }
      break;

    case MQTT_EVENT_DISCONNECTED:
      Serial.println("Disconnected from broker (auto-reconnecting...)");
      statusHandler(STATE_MQTT_DISCONNECTED);
      break;

    case MQTT_EVENT_SUBSCRIBED:
      Serial.printf("Subscribed, msg_id=%d\n", event->msg_id);
      break;

    case MQTT_EVENT_DATA: {
      Serial.printf("Message Received in topic %.*s: ", event->topic_len, event->topic);
      Serial.printf("%.*s\n", event->data_len, event->data);

      char topic[event->topic_len +1];
      memcpy(topic, event->topic, event->topic_len);
      topic[event->topic_len] = '\0';

      char *segment[MAX_SEGMENT];
      size_t tokenCount = 0;
      char *savePtr;

      char *token = strtok_r(topic, "/", &savePtr);

      while(token != NULL && tokenCount < MAX_SEGMENT) {
        segment[tokenCount] = token;
        tokenCount++;
        token = strtok_r(NULL, "/", &savePtr);
      }

      if (tokenCount < 3) return; 

      char payload[event->data_len + 1];
      memcpy(payload, event->data, event->data_len);
      payload[event->data_len] = '\0';

      if(strcmp(segment[1], username.c_str()) == 0 || strcmp(segment[1], "all") == 0) {
        if(strcmp(segment[0], "cmnd") == 0) cmnd(segment, tokenCount, payload);
        if(strcmp(segment[0], "conf") == 0) conf(segment, tokenCount, payload);
        // stat/ and tele/ are publish-only namespaces — device → cloud only
      } else {
        Serial.println("Client ID Mismatch");
      }
      
      /* 
      DEPRECIATED
      cmd(payload);
      */

      break;
    }

    case MQTT_EVENT_ERROR:
      statusHandler(STATE_ERROR);
      Serial.println("MQTT_EVENT_ERROR");
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        Serial.printf("  Transport error: %d\n", event->error_handle->esp_transport_sock_errno);
    }
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        Serial.printf("  Connection refused, code: %d\n", event->error_handle->connect_return_code);
    }
    globalErrorCounter++;
    break;

    default:
      break;
  }
}

void initMqtt() {
  statusHandler(STATE_MQTT_CONNECTING);
  esp_mqtt_client_config_t mqtt_cfg = {};
  mqtt_cfg.session.keepalive = 20;
  mqtt_cfg.broker.address.uri = brokerUri;
  mqtt_cfg.credentials.client_id = username.c_str();
  mqtt_cfg.credentials.username = username.c_str();
  mqtt_cfg.credentials.authentication.password = password.c_str();
  mqtt_cfg.broker.verification.certificate = ca_cert;
  
  static String lastWillTopic = "stat/" + username + "/onoff";
  mqtt_cfg.session.last_will.topic   = lastWillTopic.c_str();
  mqtt_cfg.session.last_will.msg     = "{\"status\": \"offline\"}";
  mqtt_cfg.session.last_will.msg_len = 0;
  mqtt_cfg.session.last_will.qos     = 2;
  mqtt_cfg.session.last_will.retain  = 1;

  client = esp_mqtt_client_init(&mqtt_cfg);
  if(client == NULL) {
    Serial.println("MQTT Initialization failed, rebooting");
    statusHandler(STATE_ERROR);
    delay(5000);
    ESP.restart();
  }

  esp_mqtt_client_register_event(client, (esp_mqtt_event_id_t) ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
}

// void mqttSubscribe(const char *subTopic, const int QoS) {
//     esp_mqtt_client_subscribe(client, subTopic, QoS);
// }

void mqttPublish(const char* pubTopic, const char *message, const int QoS, const int retain, const bool store) {
    esp_mqtt_client_enqueue(client, pubTopic, message, 0, QoS, retain, store);
}