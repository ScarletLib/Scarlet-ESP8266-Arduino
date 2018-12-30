#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WiFiSettings.h" // Make sure to put your SSID and PSK in this file.

const unsigned int LocalPortUDP = 3000;
const char* ClientName = "ESP8266-TestClient";
const IPAddress ServerAddress(192, 168, 0, 106);
const int RemotePortTCP = 1765;
const int RemotePortUDP = 2765;

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(STASSID, STAPSK); // From WiFiSettings.h
    while(WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(250);
    }
    Serial.println();
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());

    // TODO Construct
}

void loop()
{
    // TODO Tick
}