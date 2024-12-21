/*
  PsychicHTTP Server Example

  This example code is in the Public Domain (or CC0 licensed, at your option.)

  Unless required by applicable law or agreed to in writing, this
  software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
  CONDITIONS OF ANY KIND, either express or implied.
*/

/**********************************************************************************************
* Note: this demo relies on various files to be uploaded on the LittleFS partition
* PlatformIO -> Build Filesystem Image and then PlatformIO -> Upload Filesystem Image
**********************************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
// #include <WiFiMulti.h>

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <esp_sntp.h>
#include <PsychicHttp.h>
#if CONFIG_ESP_HTTPS_SERVER_ENABLE
    #include <PsychicHttpsServer.h> //uncomment this to enable HTTPS / SSL
#endif
#include "Esp.h"
#ifdef SVELTE_ESP32
    #include "svelteesp32.h"
#endif
#ifndef WIFI_SSID
    #error "You need to enter your wifi credentials. Rename secret.h to _secret.h and enter your credentials there."
#endif

//Enter your WIFI credentials in secret.h
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// Set your SoftAP credentials
const char *softap_ssid = "PsychicHttp";
const char *softap_password = "";
IPAddress softap_ip(10, 0, 0, 1);

//credentials for the /auth-basic and /auth-digest examples
const char *app_user = "admin";
const char *app_pass = "admin";
const char *app_name = "Your App";

//hostname for mdns (psychic.local)
const char *local_hostname = "psychic";

// #define PSY_ENABLE_SSL
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    bool app_enable_ssl = true;
    String server_cert;
    String server_key;
#endif

//our main server object
#ifdef CONFIG_ESP_HTTPS_SERVER_ENABLE
    PsychicHttpsServer server;
#else
    PsychicHttpServer server;
#endif
PsychicWebSocketHandler websocketHandler;
PsychicEventSource eventSource;

//NTP server stuff
const char *ntpServer1 = "pool.ntp.org";
const char *ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;
struct tm timeinfo;
// WiFiMulti WiFiMulti;

// Callback function (gets called when time adjusts via NTP)
void timeAvailable(struct timeval *t) {
    if (!getLocalTime(&timeinfo)) {
        log_i("Failed to obtain time");
        return;
    }

    log_i("NTP update: ");
    char buffer[40];
    strftime(buffer, 40, "%FT%T%z", &timeinfo);
    log_i("%s", buffer);
}

// bool connectToWifi() {
//     //dual client and AP mode
//     WiFi.mode(WIFI_AP_STA);

//     // Configure SoftAP
//     WiFi.softAPConfig(softap_ip, softap_ip, IPAddress(255, 255, 255, 0)); // subnet FF FF FF 00
//     WiFi.softAP(softap_ssid, softap_password);
//     IPAddress myIP = WiFi.softAPIP();
//     log_i("SoftAP IP Address: ", myIP.toString().c_str());
//     log_i(myIP);

//     log_i();
//     log_i("[WiFi] Connecting to ");
//     log_i(ssid);

//     WiFi.begin(ssid, password);
//     // Auto reconnect is set true as default
//     // To set auto connect off, use the following function
//     // WiFi.setAutoReconnect(false);

//     // Will try for about 10 seconds (20x 500ms)
//     int tryDelay = 500;
//     int numberOfTries = 20;

//     // Wait for the WiFi event
//     while (true) {
//         switch (WiFi.status()) {
//             case WL_NO_SSID_AVAIL:
//                 log_i("[WiFi] SSID not found");
//                 break;
//             case WL_CONNECT_FAILED:
//                 log_i("[WiFi] Failed - WiFi not connected! Reason: ");
//                 return false;
//                 break;
//             case WL_CONNECTION_LOST:
//                 log_i("[WiFi] Connection was lost");
//                 break;
//             case WL_SCAN_COMPLETED:
//                 log_i("[WiFi] Scan is completed");
//                 break;
//             case WL_DISCONNECTED:
//                 log_i("[WiFi] WiFi is disconnected");
//                 break;
//             case WL_CONNECTED:
//                 log_i("[WiFi] WiFi is connected!");
//                 log_i("[WiFi] IP address: ");
//                 log_i(WiFi.localIP());
//                 return true;
//                 break;
//             default:
//                 log_i("[WiFi] WiFi Status: ");
//                 log_i(WiFi.status());
//                 break;
//         }
//         delay(tryDelay);

//         if (numberOfTries <= 0) {
//             log_i("[WiFi] Failed to connect to WiFi!");
//             // Use disconnect function to force stop trying to connect
//             WiFi.disconnect();
//             return false;
//         } else {
//             numberOfTries--;
//         }
//     }

//     return false;
// }

void setup() {
    Serial.begin(115200);
    delay(3000);

    // We start by connecting to a WiFi network
    // To debug, please enable Core Debug Level to Verbose
    // if (connectToWifi())
    // {

    log_i("Connecting to %s %s", ssid, password);


    WiFi.begin(ssid, password);
wl_status_t  s;

    while ((s = WiFi.status()) != WL_CONNECTED) {
        delay(500);
        log_i("s=%d",s);
    }
    log_i("WiFi connected - IP address: %s", WiFi.localIP().toString().c_str());
 
    delay(500);
    //Setup our NTP to get the current time.
    sntp_set_time_sync_notification_cb(timeAvailable);
    esp_sntp_servermode_dhcp(true);
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    //set up our esp32 to listen on the local_hostname.local domain
    if (!MDNS.begin(local_hostname)) {
        log_i("Error starting mDNS");
        return;
    }
    MDNS.addService("http", "tcp", 80);

    if(!LittleFS.begin()) {
        log_i("LittleFS Mount Failed. Do Platform -> Build Filesystem Image and Platform -> Upload Filesystem Image from VSCode");
        return;
    }

    //look up our keys?
#if CONFIG_ESP_HTTPS_SERVER_ENABLE
    if (app_enable_ssl) {
        File fp = LittleFS.open("/server.crt");
        if (fp) {
            server_cert = fp.readString();

            // log_i("Server Cert:");
            // log_i(server_cert);
        } else {
            log_i("server.pem not found, SSL not available");
            app_enable_ssl = false;
        }
        fp.close();

        File fp2 = LittleFS.open("/server.key");
        if (fp2) {
            server_key = fp2.readString();

            // log_i("Server Key:");
            // log_i(server_key);
        } else {
            log_i("server.key not found, SSL not available");
            app_enable_ssl = false;
        }
        fp2.close();
    }
#endif

    //setup server config stuff here
    server.config.max_uri_handlers = 20; //maximum number of uri handlers (.on() calls)

#if CONFIG_ESP_HTTPS_SERVER_ENABLE
    server.ssl_config.httpd.max_uri_handlers = 20; //maximum number of uri handlers (.on() calls)

    //do we want secure or not?
    if (app_enable_ssl) {
        server.listen(443, server_cert.c_str(), server_key.c_str());

        //this creates a 2nd server listening on port 80 and redirects all requests HTTPS
        PsychicHttpServer *redirectServer = new PsychicHttpServer();
        redirectServer->config.ctrl_port = 20424; // just a random port different from the default one
        redirectServer->listen(80);
        redirectServer->onNotFound([](PsychicRequest *request) {
            String url = "https://" + request->host() + request->url();
            return request->redirect(url.c_str());
        });
    } else
        server.listen(80);
#else
    server.listen(80);
#endif
#ifdef SVELTE_ESP32
    initSvelteStaticFiles(&server);
#endif
    // }
}

unsigned long lastUpdate = 0;
char output[60];

void loop() {
    if (millis() - lastUpdate > 2000) {
        sprintf(output, "Millis: %d\n", millis());
        websocketHandler.sendAll(output);

        sprintf(output, "%d", millis());
        eventSource.send(output, "millis", millis(), 0);

        lastUpdate = millis();
    }
}