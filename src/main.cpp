#include <WiFiManager.h>
#include <mdns.h>
#include <ESP8266HTTPClient.h>

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#endif

#define DOMOTICZ_SERVICE_NAME "_domoticz._tcp.local"

int mdnsQueryTimeout = 0;

String domoticzPath = String("");
String domoticzHost = String("");
String domoticzPort = String("");
String domoticzAddress = String("");

String hardwareId = String("");

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
    } else {
        DEBUG_PRINT("Could not get hardware ID : " + httpCode);
    }
    http.end();
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
    String msg = String("main loop");
    msg += ESP.getCycleCount();
    DEBUG_PRINT(msg);
#endif

    if (domoticzPath == "" || domoticzPort == "" || domoticzAddress == "") {
        if (mdnsQueryTimeout == 0) {
            sendMDNSQuery(DOMOTICZ_SERVICE_NAME);
            mdnsQueryTimeout = 1;
        }
        DEBUG_PRINT("Waiting for mDNS answer...");
        mdnsClient.loop();
    } else if (hardwareId == "") {
        DEBUG_PRINT("Domoticz server is at :");
        DEBUG_PRINT("http://" + domoticzAddress + ":" + domoticzPort + domoticzPath);
        fetchHardwareId();
    } else {
        DEBUG_PRINT("hardwareId is : " + hardwareId);
    }

    delay(1000);
}
