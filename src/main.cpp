#include <Arduino.h>
#include <EEPROM.h>
#include <EmonLib.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#define DEBUG
#define USE_RF_DISABLED
#define AP_NAME          "POWER MONITOR"
#define AP_PASSWORD      "9XH4KTCBNS"

#define VERSION          101
#define HTTPS_PORT       443

#define EERPOM_BEGIN     512
#define EERPOM_ADDRESS   0
#define RTC_ADDRESS      0
#define MAX_SLEEP        300

#define GOOGLE_ID_MAXLEN 64
#define MIN_READINGS     1
#define MAX_READINGS     20
#define MIN_INTERVAL     1

// Peristant data (survives power-loss)
struct Persist {
    char google_id[GOOGLE_ID_MAXLEN] = "";
};

// Volatile Data (wiped on power-loss)
struct Config {
    byte num_readings     = 0;
    byte reading_index    = 0;
    unsigned int interval = 0;
    unsigned int sleeps   = 0;
    byte battery_mode     = 0;
    byte version          = VERSION;
    char google_id[GOOGLE_ID_MAXLEN] = "";
    unsigned int readings[MAX_READINGS];
} config;

void read_persist() {
    #ifdef DEBUG
    Serial.println("Read Persist");
    #endif

    EEPROM.begin(EERPOM_BEGIN);

    Persist persist;
    EEPROM.get(EERPOM_ADDRESS, persist);
    strncpy(config.google_id, persist.google_id, sizeof(config.google_id));
}

void write_persist() {
    #ifdef DEBUG
    Serial.println("Write Persist");
    #endif
    
    EEPROM.begin(EERPOM_BEGIN);

    Persist persist;
    strncpy(persist.google_id, config.google_id, sizeof(persist.google_id));
    EEPROM.put(EERPOM_ADDRESS, persist);
    EEPROM.commit();
}

void reset_config() {
    #ifdef DEBUG
    Serial.println("Reset Config");
    #endif

    config = Config();
    read_persist();
}

void read_config() {
    #ifdef DEBUG
    Serial.println("Read Config");
    #endif

    ESP.rtcUserMemoryRead(RTC_ADDRESS, (uint32_t*) &config, sizeof(config));
    if (config.version != VERSION) {
        reset_config();
    }
}

void write_config() {
    #ifdef DEBUG
    Serial.println("Write Config");
    #endif

    ESP.rtcUserMemoryWrite(RTC_ADDRESS, (uint32_t*) &config, sizeof(config));
}

void factory_reset() {
    #ifdef DEBUG
    Serial.println("Factory Reset");
    #endif

    config = Config();
    write_persist();
    write_config();
    WiFi.disconnect(true);
    
    ESP.reset();
    yield();
    delay(100);
}

void WiFiOn() {
    if (config.battery_mode == 1) {
        #ifdef DEBUG
        Serial.println("WiFi On");
        #endif

        WiFi.forceSleepWake();
    }
}

void WiFiOff() {
    if (config.battery_mode == 1) {
        #ifdef DEBUG
        Serial.println("WiFi Off");
        #endif

        WiFi.forceSleepBegin();
    }
}


void sleep(int interval) {
    if (config.battery_mode == 0) {
        write_config();
        #ifdef DEBUG
        Serial.print("Delay: ");
        Serial.println(interval);
        #endif
        delay(interval*1000);
        return;
    }

    if (interval > MAX_SLEEP) {
        config.sleeps = interval / MAX_SLEEP;
        interval = interval % MAX_SLEEP;
    }
    
    #ifdef USE_RF_DISABLED
    RFMode mode = WAKE_RF_DISABLED;
    if (config.sleeps == 0 && config.reading_index == (config.num_readings-1)) {
        mode = WAKE_RF_DEFAULT;
        #ifdef DEBUG
        Serial.println("RF Wake");
        #endif
    }
    #else
    RFMode mode = WAKE_RF_DEFAULT;
    #endif

    write_config();

    #ifdef DEBUG
    Serial.print("Max Sleeps: ");
    Serial.println(config.sleeps);
    Serial.print("Sleeping: ");
    Serial.println(interval);
    Serial.println("");
    #endif
    
    ESP.deepSleep(interval * 1000000, mode);
    yield();
    delay(100);
}

String send_data(String payload) {
    WiFiOn();
    
    WiFiManager wifiManager;

    #ifndef DEBUG
    wifiManager.setDebugOutput(false);
    #endif

    WiFiManagerParameter google_id("google_id", "GOOGLE ID", config.google_id, sizeof(config.google_id));
    wifiManager.addParameter(&google_id);

    if (strcmp(config.google_id, "") == 0) {
        wifiManager.startConfigPortal(AP_NAME, AP_PASSWORD);
    }
    else {
        wifiManager.setConfigPortalTimeout(180);
        wifiManager.autoConnect(AP_NAME, AP_PASSWORD);
    }

    WiFiClientSecure client;
    String line = "";
    String url = "https://script.google.com/macros/s/" + String(google_id.getValue()) + "/exec";

    #ifdef DEBUG
    Serial.println(payload);
    Serial.println(url);
    #endif

    client.connect("script.google.com", HTTPS_PORT);
    client.print(String("POST ") + url + " HTTP/1.1\r\n"
        "Host: script.google.com\r\n" +
        "Content-Type: application/json\r\n" +
        "Connection: close\r\n" +
        "Content-Length: " + payload.length() + "\r\n" +
        "\r\n" + payload);

    url = "";
    while (client.connected()) {
        line = client.readStringUntil('\n');
        if (line.startsWith("Location:")) {
            line.replace("Location: ",  "");
            url = line;
            break;
        }
    }

    if (url == "") {
        #ifdef DEBUG
        Serial.println("Error pushing readings");
        #endif
        return "";
    }

    #ifdef DEBUG
    Serial.println(url);
    #endif

    client.connect("script.googleusercontent.com", HTTPS_PORT);
    client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                "Host: script.googleusercontent.com\r\n" +
                "User-Agent: ESP8266\r\n" +
                "Connection: close\r\n\r\n");

    while (client.connected()) {
        line = client.readStringUntil('\n');
        if (line == "\r") {
            break;
        }
    }

    line = client.readStringUntil('\n');
    client.stop();

    WiFiOff();

    // If we have a good response, let's save the google_id provided
    if (line != "" && strcmp(config.google_id, google_id.getValue()) != 0) {
        strlcpy(config.google_id, google_id.getValue(), sizeof(config.google_id));
        write_persist();
    }

    return line;
}

void push_readings() {
    DynamicJsonBuffer jsonBuffer;
    JsonObject& root = jsonBuffer.createObject();

    root["m"] = WiFi.macAddress();
    root["i"] = config.interval;

    JsonArray& data = root.createNestedArray("r");
    for (int index = 0; index < config.num_readings; index++) {
        data.add(config.readings[index]);
    }

    String payload;
    root.printTo(payload);
    payload = send_data(payload);
    if (payload == "") {
        sleep(config.interval);
        return;
    }

    #ifdef DEBUG
    Serial.println(payload);
    #endif

    JsonObject& resp = jsonBuffer.parseObject(payload);

    if ((resp["f"] | 0) == 1) {
        factory_reset();
    }

    config.num_readings = constrain(resp["n"] | config.num_readings, MIN_READINGS, MAX_READINGS);
    config.interval = _max(resp["i"] | config.interval, MIN_INTERVAL);
    config.battery_mode = constrain(resp["b"] | config.battery_mode, 0, 1);

    int sleep_for = resp["s"] | config.interval;
    
    sleep(sleep_for);
}

int read_power() {
    EnergyMonitor emon1;

    emon1.current(A0, 90.9); // input pin, calibration (2000/burden resistance)

    double Irms = emon1.calcIrms(1480);
    Irms = emon1.calcIrms(1480);
    Irms = emon1.calcIrms(1480);
    Irms = emon1.calcIrms(1480);

    #ifdef DEBUG
    Irms = (config.reading_index+1)*10;
    #endif

    if (Irms <= 0.0) {
        Irms = 0;
    }
    else {
        Irms = Irms*100;
    }

    return (int)Irms;
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

    if (reason != "Deep-Sleep Wake") {
        reset_config();
    }
    else {
        read_config();
    }

    if (config.sleeps > 0) {
        config.sleeps -= 1;
        sleep(MAX_SLEEP);
    }

    WiFiOff();

    delay(100); //seems to fix crashes
}

void loop() {
    #ifdef DEBUG
    Serial.printf("Loop Heap Size: %u\n", ESP.getFreeHeap());
    #endif

    if (config.reading_index < config.num_readings) {
        int power = read_power();
        config.readings[config.reading_index] = power;
        config.reading_index++;

        #ifdef DEBUG
        Serial.printf("Reading %d/%d = %d\n", config.reading_index, config.num_readings, power);
        #endif
    }

    if (config.reading_index < config.num_readings) {
        sleep(config.interval);
    }
    else {
        config.reading_index = 0;
        push_readings();
    }
}