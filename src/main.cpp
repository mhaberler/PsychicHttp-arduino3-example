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

// Callback function (gets called when time adjusts via NTP)
void timeAvailable(struct timeval *t)
{
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.print("NTP update: ");
  char buffer[40];
  strftime(buffer, 40, "%FT%T%z", &timeinfo);
  Serial.println(buffer);
}

bool connectToWifi()
{
  //dual client and AP mode
  WiFi.mode(WIFI_AP_STA);

  // Configure SoftAP
  WiFi.softAPConfig(softap_ip, softap_ip, IPAddress(255, 255, 255, 0)); // subnet FF FF FF 00
  WiFi.softAP(softap_ssid, softap_password);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("SoftAP IP Address: ");
  Serial.println(myIP);

  Serial.println();
  Serial.print("[WiFi] Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  // Auto reconnect is set true as default
  // To set auto connect off, use the following function
  // WiFi.setAutoReconnect(false);

  // Will try for about 10 seconds (20x 500ms)
  int tryDelay = 500;
  int numberOfTries = 20;

  // Wait for the WiFi event
  while (true)
  {
    switch (WiFi.status())
    {
      case WL_NO_SSID_AVAIL:
        Serial.println("[WiFi] SSID not found");
        break;
      case WL_CONNECT_FAILED:
        Serial.print("[WiFi] Failed - WiFi not connected! Reason: ");
        return false;
        break;
      case WL_CONNECTION_LOST:
        Serial.println("[WiFi] Connection was lost");
        break;
      case WL_SCAN_COMPLETED:
        Serial.println("[WiFi] Scan is completed");
        break;
      case WL_DISCONNECTED:
        Serial.println("[WiFi] WiFi is disconnected");
        break;
      case WL_CONNECTED:
        Serial.println("[WiFi] WiFi is connected!");
        Serial.print("[WiFi] IP address: ");
        Serial.println(WiFi.localIP());
        return true;
        break;
      default:
        Serial.print("[WiFi] WiFi Status: ");
        Serial.println(WiFi.status());
        break;
    }
    delay(tryDelay);

    if (numberOfTries <= 0)
    {
      Serial.print("[WiFi] Failed to connect to WiFi!");
      // Use disconnect function to force stop trying to connect
      WiFi.disconnect();
      return false;
    }
    else
    {
      numberOfTries--;
    }
  }

  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(10);

  // We start by connecting to a WiFi network
  // To debug, please enable Core Debug Level to Verbose
  if (connectToWifi())
  {
    //Setup our NTP to get the current time.
    sntp_set_time_sync_notification_cb(timeAvailable);
    esp_sntp_servermode_dhcp(true); 
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);

    //set up our esp32 to listen on the local_hostname.local domain
    if (!MDNS.begin(local_hostname)) {
      Serial.println("Error starting mDNS");
      return;
    }
    MDNS.addService("http", "tcp", 80);

    if(!LittleFS.begin())
    {
      Serial.println("LittleFS Mount Failed. Do Platform -> Build Filesystem Image and Platform -> Upload Filesystem Image from VSCode");
      return;
    }

    //look up our keys?
    #if CONFIG_ESP_HTTPS_SERVER_ENABLE
      if (app_enable_ssl)
      {
        File fp = LittleFS.open("/server.crt");
        if (fp)
        {
          server_cert = fp.readString();

          // Serial.println("Server Cert:");
          // Serial.println(server_cert);
        }
        else
        {
          Serial.println("server.pem not found, SSL not available");
          app_enable_ssl = false;
        }
        fp.close();

        File fp2 = LittleFS.open("/server.key");
        if (fp2)
        {
          server_key = fp2.readString();

          // Serial.println("Server Key:");
          // Serial.println(server_key);
        }
        else
        {
          Serial.println("server.key not found, SSL not available");
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
      if (app_enable_ssl)
      {
        server.listen(443, server_cert.c_str(), server_key.c_str());
        
        //this creates a 2nd server listening on port 80 and redirects all requests HTTPS
        PsychicHttpServer *redirectServer = new PsychicHttpServer();
        redirectServer->config.ctrl_port = 20424; // just a random port different from the default one
        redirectServer->listen(80);
        redirectServer->onNotFound([](PsychicRequest *request) {
          String url = "https://" + request->host() + request->url();
          return request->redirect(url.c_str());
        });
      }
      else
        server.listen(80);
    #else
      server.listen(80);
    #endif
#ifdef SVELTE_ESP32
    initSvelteStaticFiles(&server);
#endif
  }
}

unsigned long lastUpdate = 0;
char output[60];

void loop()
{
  if (millis() - lastUpdate > 2000)
  {
    sprintf(output, "Millis: %d\n", millis());
    websocketHandler.sendAll(output);

    sprintf(output, "%d", millis());
    eventSource.send(output, "millis", millis(), 0);

    lastUpdate = millis();
  }
}