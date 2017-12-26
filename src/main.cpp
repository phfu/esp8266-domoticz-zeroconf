#include <WiFiManager.h>
#include <mdns.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <SimpleDHT.h>

#include <ESP8266WiFi.h>

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#endif

#define DOMOTICZ_SERVICE_NAME "_domoticz._tcp.local"

int mdnsQueryTimeout = 0;

// hardcoded to skip mDNS queries, because shitty wifi router :(
// set to empty string to enable mDNS resolution
String domoticzPath = String("/domoticz");
String domoticzHost = String("nas.local");
String domoticzPort = String("8181");
String domoticzAddress = String("192.168.0.123");

String hardwareId = String("");
String deviceId = String("");

SimpleDHT22 dht22;
// pin D5
#define DHT22_PIN 14

// wifi AP/config captive portal mode callback
void wifiConfigCallback(WiFiManager *wiFiManager) {
    DEBUG_PRINT("Entered config mode");
    DEBUG_PRINT(WiFi.softAPIP());
    // if you used auto generated SSID, print it
    DEBUG_PRINT(wiFiManager->getConfigPortalSSID());
}

// When an mDNS packet gets parsed this optional callback gets called once per
// Query.
// See mdns.h for definition of mdns::Answer.
void mdnsAnswerCallback(const mdns::Answer *answer) {
    String answerName = String(answer->name_buffer);
    if (answerName.indexOf(DOMOTICZ_SERVICE_NAME) >= 0) {

#ifdef DEBUG
        DEBUG_PRINT("Got mDNS answer for domoticz");
        answer->Display();
#endif

        if (answer->rrtype == MDNS_TYPE_SRV) {
            // host / port answer
            // RRDATA:    p=0;w=0;port=8181;host=nas.local
            String answerData = String(answer->rdata_buffer);
            int portInfoIndex = answerData.indexOf("port=");
            int hostInfoIndex = answerData.indexOf("host=");

            domoticzHost = String(answerData.substring(hostInfoIndex + 5));
            domoticzPort = String(answerData.substring(portInfoIndex + 5, hostInfoIndex - 1));
        } else if (answer->rrtype == MDNS_TYPE_TXT) {
            // txt record answer
            //  RRDATA:    ␎path=/domoticz
            String answerData = String(answer->rdata_buffer);
            int pathInfoIndex = answerData.indexOf("path=");
            domoticzPath = String(answerData.substring(pathInfoIndex + 5));
        }
    } else if (answer->rrtype == MDNS_TYPE_A) {
        DEBUG_PRINT("Got type A mDNS answer");
        String answerName = String(answer->name_buffer);
        if (answerName == domoticzHost) {
            domoticzAddress = String(answer->rdata_buffer);
        }
    } else {
        DEBUG_PRINT("Got unrelated mDNS answer");
    }
}

mdns::MDns mdnsClient(NULL, NULL, mdnsAnswerCallback);

void sendMDNSQuery(const char *name) {
    DEBUG_PRINT("query mDNS.");
    mdns::Query q;

    int len = strlen(name);

    for (int i = 0; i < len; i++) {
        q.qname_buffer[i] = name[i];
    }
    q.qname_buffer[len] = '\0';

    q.qtype = 0x0c; // type 12 : PTR
    q.qclass = 0x01;
    q.unicast_response = 0;
    q.valid = 0;

    mdnsClient.Clear();
    mdnsClient.AddQuery(q);
    mdnsClient.Send();
}

void declareHardware() {
    String url = "http://";
    url += domoticzAddress;
    url += ":";
    url += domoticzPort;
    url += domoticzPath;
    url += "/json.htm?type=command&param=addhardware&htype=15&enabled=true&datatimeout=0&name=";
    url += "ESP_";
    url += ESP.getChipId();

    DEBUG_PRINT("Querying GET " + url);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    DEBUG_PRINT(httpCode);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DEBUG_PRINT("Got hardware create answer : ");
        DEBUG_PRINT(payload);

        const size_t bufferSize = JSON_OBJECT_SIZE(3) + 40;
        DynamicJsonBuffer jsonBuffer(bufferSize);
        JsonObject& root = jsonBuffer.parseObject(payload);

        hardwareId = String((const char*)root["idx"]);
    } else {
        DEBUG_PRINT("Could not create hardware : " + httpCode);
    }
    http.end();
}

void parseHardwareResponse(String json) {
    int guessedHardwareCount = json.length() / 400;
    const size_t bufferSize = JSON_ARRAY_SIZE(guessedHardwareCount) + JSON_OBJECT_SIZE(guessedHardwareCount) + guessedHardwareCount*JSON_OBJECT_SIZE(17) + json.length() / 2;
    DynamicJsonBuffer jsonBuffer(bufferSize);

    JsonObject& root = jsonBuffer.parseObject(json);
    JsonArray& result = root["result"];

    int hardwareCount = result.size();
    DEBUG_PRINT("Found declared hardware count : ");
    DEBUG_PRINT(hardwareCount);
    DEBUG_PRINT("Searching for our id : ");
    DEBUG_PRINT(ESP.getChipId());

    int i = 0;
    while (hardwareId == "" && i < hardwareCount) {
        JsonObject& hardware = result[i++];
        String hardwareName = String((const char*)hardware["Name"]);
        String chipId = String(ESP.getChipId());
        if (hardwareName.indexOf(chipId) >= 0) {
            DEBUG_PRINT("Found matching hardware with name : ");
            DEBUG_PRINT(hardwareName);
            hardwareId = String((const char*)hardware["idx"]);
        } else {
            DEBUG_PRINT("Hardware name does not match : ");
            DEBUG_PRINT(hardwareName);
        }
    }

    if (hardwareId == "") {
        DEBUG_PRINT("Hardware not declared yet, creating...");
        declareHardware();
    } else {
        DEBUG_PRINT("Got hardware IDX : ");
        DEBUG_PRINT(hardwareId);
    }
}

void fetchHardwareId() {
    DEBUG_PRINT("Fetching hardware ID...");
    String url = "http://";
    url += domoticzAddress;
    url += ":";
    url += domoticzPort;
    url += domoticzPath;
    url += "/json.htm?type=hardware";

    DEBUG_PRINT("Querying GET " + url);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    DEBUG_PRINT(httpCode);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DEBUG_PRINT("Got hardware answer : ");
        DEBUG_PRINT(payload);
        parseHardwareResponse(payload);
    } else {
        DEBUG_PRINT("Could not get hardware ID : " + httpCode);
    }
    http.end();
}


void declareDevice() {
    String url = "http://";
    url += domoticzAddress;
    url += ":";
    url += domoticzPort;
    url += domoticzPath;
    url += "/json.htm?type=createvirtualsensor&sensorname=Temperature&sensortype=82&idx=";
    url += hardwareId;

    DEBUG_PRINT("Querying GET " + url);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    DEBUG_PRINT(httpCode);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DEBUG_PRINT("Got device create answer : ");
        DEBUG_PRINT(payload);

        const size_t bufferSize = JSON_OBJECT_SIZE(3) + 40;
        DynamicJsonBuffer jsonBuffer(bufferSize);
        JsonObject& root = jsonBuffer.parseObject(payload);

        deviceId = String((const char*)root["idx"]);
    } else {
        DEBUG_PRINT("Could not create device : " + httpCode);
    }
    http.end();
}

void parseDevicesResponse(String json) {
    int guessedDevicesCount = json.length() / 1200;
    const size_t bufferSize = JSON_ARRAY_SIZE(guessedDevicesCount) + JSON_OBJECT_SIZE(guessedDevicesCount) + guessedDevicesCount*JSON_OBJECT_SIZE(36) + json.length();
    DynamicJsonBuffer jsonBuffer(bufferSize);

    JsonObject& root = jsonBuffer.parseObject(json);
    JsonArray& result = root["result"];

    int devicesCount = result.size();
    DEBUG_PRINT("Found declared devices count : ");
    DEBUG_PRINT(devicesCount);
    DEBUG_PRINT("Searching for device with hardware id : ");
    DEBUG_PRINT(hardwareId);

    int i = 0;
    while (deviceId == "" && i < devicesCount) {
        JsonObject& device = result[i++];
        String deviceName = String((const char*)device["Name"]);
        String deviceHardwareId = String((const char*)device["HardwareID"]);
        if (deviceHardwareId == hardwareId && deviceName.indexOf("Temperature") >= 0) {
            DEBUG_PRINT("Found matching device");
            deviceId = String((const char*)device["idx"]);
        }
    }

    if (deviceId == "") {
        DEBUG_PRINT("Device not declared yet, creating...");
        declareDevice();
    } else {
        DEBUG_PRINT("Got device IDX : ");
        DEBUG_PRINT(deviceId);
    }
}

void fetchDeviceId() {
    DEBUG_PRINT("Fetching device ID...");
    String url = "http://";
    url += domoticzAddress;
    url += ":";
    url += domoticzPort;
    url += domoticzPath;
    url += "/json.htm?type=devices&filter=temp";

    DEBUG_PRINT("Querying GET " + url);
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    DEBUG_PRINT(httpCode);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DEBUG_PRINT("Got devices answer : ");
        DEBUG_PRINT(payload);
        parseDevicesResponse(payload);
    } else {
        DEBUG_PRINT("Could not get device ID : " + httpCode);
    }
    http.end();
}

void readTemperature() {
    float temperature = 0;
    float humidity = 0;
    int chk = dht22.read2(DHT22_PIN, &temperature, &humidity, NULL);
    if (chk == SimpleDHTErrSuccess) {
        DEBUG_PRINT("DHT read ok : ");
        DEBUG_PRINT(temperature);
        DEBUG_PRINT(humidity);

        DEBUG_PRINT("Wifi status :");
        DEBUG_PRINT(WiFi.status());

        String url = "http://";
        url += domoticzAddress;
        url += ":";
        url += domoticzPort;
        url += domoticzPath;
        url += "/json.htm?type=command&param=udevice&idx=";
        url += deviceId;
        url += "&nvalue=0&svalue=";
        url += temperature;
        url += ";";
        url += humidity;
        url += ";0";

        DEBUG_PRINT("Querying GET " + url);
        HTTPClient http;
        http.begin(url);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            DEBUG_PRINT("Update sensor OK :");
            String payload = http.getString();
            DEBUG_PRINT(payload);
        } else {
            DEBUG_PRINT("Could not update sensor : ");
            DEBUG_PRINT(http.errorToString(httpCode).c_str());
        }
        http.end();
    } else {
        DEBUG_PRINT("DHT read error : ");
        DEBUG_PRINT(chk);
    }
}

void setup() {
#ifdef DEBUG
    Serial.begin(115200);
#endif

    String accessPointName = "ESP8622-";
    accessPointName += ESP.getChipId();

    WiFiManager wifiManager;

// wifiManager.resetSettings();

#ifdef DEBUG
    wifiManager.setDebugOutput(true);
#else
    wifiManager.setDebugOutput(false);
#endif

    wifiManager.setAPCallback(wifiConfigCallback);

    if (!wifiManager.autoConnect(accessPointName.c_str(), NULL)) {
        DEBUG_PRINT("failed to connect to any wifi");
        ESP.reset();
        delay(1000);
    }

    DEBUG_PRINT("Wifi connected.");
}

void loop() {
#ifdef DEBUG
    String msg = String("main loop, cycle count:");
    msg += ESP.getCycleCount();
    msg += " free heap:";
    msg += ESP.getFreeHeap();
    DEBUG_PRINT(msg);
#endif

    if (domoticzPath == "" || domoticzPort == "" || domoticzAddress == "") {
        if (mdnsQueryTimeout == 0) {
            sendMDNSQuery(DOMOTICZ_SERVICE_NAME);
            mdnsQueryTimeout = 1;
        }
        DEBUG_PRINT("Waiting for mDNS answer...");
        mdnsClient.loop();
        delay(1000);
    } else if (hardwareId == "") {
        DEBUG_PRINT("Domoticz server is at :");
        DEBUG_PRINT("http://" + domoticzAddress + ":" + domoticzPort + domoticzPath);
        fetchHardwareId();
    } else if (deviceId == "") {
        DEBUG_PRINT("hardwareId is : " + hardwareId);
        fetchDeviceId();
    } else {
        DEBUG_PRINT("deviceId is : " + deviceId);
        readTemperature();
        delay(60*1000);
    }
}
