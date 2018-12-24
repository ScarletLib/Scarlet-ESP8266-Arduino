#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WiFiSettings.h" // Make sure to put your SSID and PSK in this file.

const unsigned int LocalPortUDP = 3000;

const char* Host = "192.168.0.106";
const int RemotePortTCP = 1765;
const int RemotePortUDP = 2765;

char PacketBufferUDP[UDP_TX_PACKET_MAX_SIZE];
char ReplyBufferUDP[UDP_TX_PACKET_MAX_SIZE];

WiFiUDP UDP;
WiFiClient TCP;

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(STASSID, STAPSK);
    while(WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(500);
    }
    Serial.println();
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
    
    //UDP.begin(LocalPortUDP); // Listen for incoming packets.
}

void loop()
{
    // Make connections
    Serial.println("=====");
    Serial.println("Attempting server connections...");

    if(!TCP.connect(Host, RemotePortTCP))
    {
        Serial.println("Server TCP connection failed, retrying in 5s...");
        delay(5000);
        return;
    }

    // TODO: Allow client name customization
    // TODO: Send some kind of timestamp?
    // TODO: Properly generate length (not hardcoded)
    // TODO: Properly send local UDP port (not hardcoded)
    byte HandshakeTCP[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp
                        0xF4, // Packet ID (HANDSHAKE_FROM_CLIENT)
                        0, 21, // Length
                        0x00, // Latency Management (NONE)
                        0xC0, // Version
                        0x0B, 0xB8, // Local UDP port (3000)
                        0x00, 0x45, 0x00, 0x53, 0x00, 0x50}; // Name ("ESP")
    
    if(TCP.connected())
    {
        Serial.println("Sending handshake to server...");
        TCP.write(HandshakeTCP, sizeof(HandshakeTCP));
        Serial.println("Handshake sent.");
    }

    unsigned long HandshakeWaitStart = millis();
    while(TCP.available() == 0)
    {
        if(millis() - HandshakeWaitStart > 5000)
        {
            Serial.println("Did not receive handshake response.");
            TCP.stop();
            delay(5000);
            return;
        }
    }

    unsigned int HandshakeRespLen = TCP.available();
    byte* HandshakeRespBuffer = new byte[HandshakeRespLen];
    for(int i = 0; i < HandshakeRespLen; i++)
    {
        HandshakeRespBuffer[i] = TCP.read();
    }
    Serial.print("Received handshake response from server of length ");
    Serial.println(HandshakeRespLen);

    if(HandshakeRespLen < 13)
    {
        Serial.println("Handshake response length inadequate. Retrying in 5s...");
        delay(5000);
        return;
    }

    if(HandshakeRespBuffer[8] != 0xF5) // Packet ID (HANDSHAKE_FROM_SERVER)
    {
        Serial.print("Expected handshake response, got ID ");
        Serial.print(HandshakeRespBuffer[8]);
        Serial.println(" instead. Retrying in 5s...");
        delay(5000);
        return;
    }
    // TODO: Check packet length.
    Serial.print("Server is on Scarlet version ");
    Serial.print(HandshakeRespBuffer[11]); // Server version
    Serial.println('.');

    // For future use if version incompability gets introduced.
    /*if(HandshakeRespBuffer[11] > 0x10)
    {
        Serial.println("This version is not compatible. Retrying in 60s...");
        delay(60000);
        return;
    }*/

    if(HandshakeRespBuffer[12] == 0x00) // Connection result (OKAY)
    {
        Serial.println("TCP client successfully connected!");
    }
    else if(HandshakeRespBuffer[12] == 0x01) // Connection result (INVALID_NAME)
    {
        Serial.println("Server rejected client name. Retrying in 5s...");
        delay(5000);
        return;
    }
    else if(HandshakeRespBuffer[12] == 0x02) // Connection result (INCOMPATIBLE_VERSIONS)
    {
        Serial.println("Server rejected client due to incompatible versions. Retrying in 60s...");
        delay(60000);
        return;
    }
    else if(HandshakeRespBuffer[12] == 0x03) // Connection result (CONNECTION_FAILED)
    {
        Serial.println("Server returned a general error. Retrying in 5s...");
        delay(5000);
        return;
    }
    else // Connection result unknown
    {
        Serial.println("Server returned an unknown handshake response type. Retrying in 5s...");
        delay(5000);
        return;
    }

    delay(10000);


/*
    int PacketSize = UDP.parsePacket();
    if(PacketSize > 0)
    {
        Serial.print("Received packet of size ");
        Serial.print(PacketSize);
        Serial.print(" from ");
        IPAddress RemoteIP = UDP.RemoteIP();
        for(int i = 0; i < 4; i++)
        {
            Serial.print(RemoteIP[i], DEC);
            if(i < 3) { Serial.print('.'); }
        }
        Serial.print(':');
        Serial.println(UDP.remotePort());

        UDP.read(PacketBufferUDP, UDP_TX_PACKET_MAX_SIZE);
        Serial.print("Contents: ");
        Serial.println(PacketBuffer);

        UDP.beginPacket(UDP.RemoteIP(), UDP.RemotePort());
        UDP.write(ReplyBufferUDP);
        UDP.endPacket();
    }*/
    delay(10);
}