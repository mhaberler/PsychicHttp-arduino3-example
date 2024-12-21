// dependencies: mlesniew/PicoMQTT hoeken/PsychicHTTP
// platform: pioarduino 3.1.0
// filesystem: littlefs
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <LittleFS.h>

#include <PicoMQTT.h>
#include <PsychicHttpsServer.h>
#include <PsychicWebSocketProxy.h>
#include <PicoSettings.h>

#if __has_include("config.h")
    #include "config.h"
#endif

#ifndef WIFI_SSID
    #define WIFI_SSID "WiFi SSID"
#endif

#ifndef WIFI_PASSWORD
    #define WIFI_PASSWORD "password"
#endif

// define a PsychicWebSocketProxy::Server object, which will connect
// the async PsychicHTTP server with the synchronous PicoMQTT library.
PsychicWebSocketProxy::Server websocket_handler;

// Initialize a PicoMQTT::Server using the PsychicWebSocketProxy::Server
// object as the server to use.
PicoMQTT::Server mqtt(websocket_handler);

// Setup a PsychicHttpsServer as usual
String server_cert;
String server_key;
PsychicHttpsServer server;

unsigned long lastUpdate = 0;

PicoSettings settings(mqtt, "testns");

PicoSettings::Setting<String> ssid(settings, "ssid", "some-SSID");
PicoSettings::Setting<String> password(settings, "pass", "secret");

PicoSettings::Setting<int> bar(settings, "bar", 42);
PicoSettings::Setting<String> baz(settings, "baz", "The Answer.");
PicoSettings::Setting<double> dzero(settings, "dzero", 0.0);
PicoSettings::Setting<double> dparam(settings, "dparam", PI);
void on_fparam_change(void);
PicoSettings::Setting<float> fparam(settings, "fparam", 2.71828182845904523536, on_fparam_change);
PicoSettings::Setting<bool> flag(settings, "flag", true, [] {
    log_i("flag=%d", flag.get());
});

// value change callback
void on_fparam_change(void) {
    log_i("fparam changed to %f, default value: %f",
          fparam.get(), fparam.get_default());
}

void setup() {
    // Setup serial
    Serial.begin(115200);

    // Connect to WiFi
    Serial.printf("Connecting to WiFi %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
    }
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
    settings.begin();

    MDNS.begin("picomqtt");

    // Setup server
    server.config.max_uri_handlers = 20;
    {
        LittleFS.begin();
        File fp = LittleFS.open("/server.crt");
        if (fp) {
            server_cert = fp.readString();
            fp.close();
        }

        fp = LittleFS.open("/server.key");
        if (fp) {
            server_key = fp.readString();
            fp.close();
        }
    }
    // Some strict clients require that the websocket subprotocol is mqtt.
    // NOTE: The subprotocol must be set *before* attaching the handler to a
    // server path using server.on(...)
    websocket_handler.setSubprotocol("mqtt");

    server.listen(443, server_cert.c_str(), server_key.c_str());

    // bind the PsychicWebSocketProxy::Server to an url like a websocket handler
    server.on("/mqtt", &websocket_handler);

    // Subscribe to a topic and attach a callback
    mqtt.subscribe("#", [](const char * topic, const char * payload) {
        // payload might be binary, but PicoMQTT guarantees that it's zero-terminated
        static unsigned int num = 0;
        Serial.printf("Received message %u in topic '%s': %s\n", ++num, topic, payload);
    });

    mqtt.begin();

    settings.publish();
    bar.change_callback = [] {
        log_i("bar changed to %d", bar.get());
    };
}

void loop() {
    if (millis() - lastUpdate > 2000) {
        //create a response object
        JsonDocument output;

        output["freeHeap"] = ESP.getFreeHeap();
        output["usedPSRam"] = ESP.getPsramSize() - ESP.getFreePsram();
        auto publish = mqtt.begin_publish("memory", measureJson(output));
        serializeJson(output, publish);
        publish.send();

        settings.publish();

        lastUpdate = millis();
    }
    mqtt.loop();
}