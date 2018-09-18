#include <Arduino.h>
#include <EEPROM.h>
#include <EmonLib.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#define DEBUG
#define AP_NAME          "POWER MONITOR"
#define AP_PASSWORD      "9XH4KTCBNS"

WiFiClient espClient;
PubSubClient client(espClient);

struct Config {
    char mqtt_server[64]   = "";
    char mqtt_user[64]     = "";
    char mqtt_password[64] = "";
};

void setup_wifi(bool forcePortal = false) {    
    WiFiManager wifiManager;

    #ifndef DEBUG
    wifiManager.setDebugOutput(false);
    #endif

    EEPROM.begin(512);

    Config config;
    EEPROM.get(0, config);

    WiFiManagerParameter mqtt_server("mqtt_server", "MQTT Server", config.mqtt_server, sizeof(config.mqtt_server));
    WiFiManagerParameter mqtt_user("mqtt_user", "MQTT User", config.mqtt_user, sizeof(config.mqtt_user));
    WiFiManagerParameter mqtt_password("mqtt_password", "MQTT Password", config.mqtt_password, sizeof(config.mqtt_password));

    wifiManager.addParameter(&mqtt_server);
    wifiManager.addParameter(&mqtt_user);
    wifiManager.addParameter(&mqtt_password);

    if (forcePortal || strcmp(config.mqtt_server, "") == 0) {
        wifiManager.startConfigPortal(AP_NAME, AP_PASSWORD);
    }
    else {
        wifiManager.setConfigPortalTimeout(180);
        wifiManager.autoConnect(AP_NAME, AP_PASSWORD);
    }

    client.setServer(mqtt_server.getValue(), 1883);
    if (client.connect("ESP8266Client", mqtt_user.getValue(), mqtt_password.getValue())) {
        #ifdef DEBUG
        Serial.println("Connected to MQTT broker");
        #endif

        if (strcmp(config.mqtt_server, mqtt_server.getValue()) != 0
            || strcmp(config.mqtt_user, mqtt_user.getValue()) != 0
            || strcmp(config.mqtt_password, mqtt_password.getValue()) != 0) {

            #ifdef DEBUG
            Serial.println("Saving new config..");
            #endif

            strlcpy(config.mqtt_server, mqtt_server.getValue(), sizeof(config.mqtt_server));
            strlcpy(config.mqtt_user, mqtt_user.getValue(), sizeof(config.mqtt_user));
            strlcpy(config.mqtt_password, mqtt_password.getValue(), sizeof(config.mqtt_password));

            EEPROM.put(0, config);
            EEPROM.commit();
        }
    }
    else {
        #ifdef DEBUG
        Serial.println("Failed to connect to MQTT broker");
        #endif
        setup_wifi(true);
    }
}

void setup() {
    #ifdef DEBUG
    Serial.begin(9600);
    Serial.println();
    Serial.println();
    Serial.println("......");
    Serial.printf("Heap size: %u\n", ESP.getFreeHeap());
    #endif

    String reason = ESP.getResetReason();
    #ifdef DEBUG
    Serial.println(reason);
    #endif

    setup_wifi();
}

void loop() {

}