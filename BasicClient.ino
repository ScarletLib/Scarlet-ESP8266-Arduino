#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WiFiSettings.h" // Make sure to put your SSID and PSK in this file.

#define TRACE_LOGGING true

const unsigned int LocalPortUDP = 3000;
const unsigned int WatchdogTimeout = 5000;

const IPAddress ServerAddress(192, 168, 0, 106);
const int RemotePortTCP = 1765;
const int RemotePortUDP = 2765;

char PacketBufferUDP[UDP_TX_PACKET_MAX_SIZE];
char ReplyBufferUDP[UDP_TX_PACKET_MAX_SIZE];
unsigned long LastWatchdog = 0;

void (*PacketHandlers[0xFF])(byte* Packet, bool IsUDP); // Array of function pointers (packet handling functions).

WiFiUDP UDP;
WiFiClient TCP;

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

    PacketHandlers[0xF0] = handleWatchdog; // WATCHDOG_FROM_SERVER
    PacketHandlers[0xF1] = handleInvalid; // WATCHDOG_FROM_CLIENT, should never be received by another client.
    // TODO: Implement remaining packet handlers.
    //PacketHandlers[0xF2] = handlePacketBufferResize; // BUFFER_LENGTH_CHANGE
    //PacketHandlers[0xF3] = handleTimeSync; // TIME_SYNCHRONIZATION
    PacketHandlers[0xF4] = handleInvalid; // HANDSHAKE_FROM_CLIENT, should never be received by another client.
    PacketHandlers[0xF5] = handleInvalid; // HANDSHAKE_FROM_SERVER, should not be received outside of the connection building block, so is ignored otherwise.
}

void loop()
{
    // Make connections
    Serial.println("=====");
    Serial.println("Attempting server connections...");

    if(!TCP.connect(ServerAddress, RemotePortTCP))
    {
        Serial.println("Server TCP connection failed, retrying in 5s...");
        delay(5000);
        return;
    }

    // TODO: Allow client name customization
    // TODO: Properly generate length (not hardcoded)
    byte HandshakeTCP[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp placeholder
                        0xF4, // Packet ID (HANDSHAKE_FROM_CLIENT)
                        0, 21, // Length
                        0x00, // Latency Management (NONE)
                        0xC0, // Version
                        ((LocalPortUDP >> 8) & 0xFF), (LocalPortUDP & 0xFF), // Local UDP port
                        0x00, 0x45, 0x00, 0x53, 0x00, 0x50}; // Name ("ESP")
    addTimestamp(HandshakeTCP);

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
    
    short ExpectedPacketLength = (HandshakeRespBuffer[9] << 8) | HandshakeRespBuffer[10];
    if(ExpectedPacketLength != HandshakeRespLen) // Size mismatch.
    {
        Serial.print("Handshake expected length of ");
        Serial.print(ExpectedPacketLength, DEC);
        Serial.print(" does not match received length ");
        Serial.print(HandshakeRespLen, DEC);
        Serial.println(". Retrying in 5s...");
        delay(5000);
        return;
    }

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

    // Make UDP connection
    UDP.begin(LocalPortUDP); // Listen for incoming packets.

    // Prepare watchdog timer
    LastWatchdog = millis();

    do // Receive and send until there is a connection issue
    {
        // Receive UDP
        int PacketSizeUDP = UDP.parsePacket();
        if(PacketSizeUDP > 0) // We received a UDP packet
        {
            if(TRACE_LOGGING)
            {
                Serial.print("Received UDP packet of size ");
                Serial.print(PacketSizeUDP);
                Serial.print(" from ");
                IPAddress RemoteIP = UDP.remoteIP();
                for(int i = 0; i < 4; i++)
                {
                    Serial.print(RemoteIP[i], DEC);
                    if(i < 3) { Serial.print('.'); }
                }
                Serial.print(':');
                Serial.println(UDP.remotePort());
            }
            UDP.read(PacketBufferUDP, UDP_TX_PACKET_MAX_SIZE);
            if(TRACE_LOGGING)
            {
                Serial.print("Contents: ");
                printHexArray(PacketBufferUDP, PacketSizeUDP);
                Serial.println();
            }
            if(PacketSizeUDP >= 11) // Valid packet.
            {
                short ExpectedPacketLength = (PacketBufferUDP[9] << 8) | PacketBufferUDP[10];
                if(ExpectedPacketLength != PacketSizeUDP) // Size mismatch.
                {
                    Serial.print("UDP packet expected length of ");
                    Serial.print(ExpectedPacketLength, DEC);
                    Serial.print(" does not match received length ");
                    Serial.print(PacketSizeUDP, DEC);
                    Serial.println(" and will be ignored.");
                }
                else if(PacketHandlers[PacketBufferUDP[8]] == nullptr) { handleUnknown(subArray(PacketBufferUDP, PacketSizeUDP), true); }
                else { (*PacketHandlers[PacketBufferUDP[8]])(subArray(PacketBufferUDP, PacketSizeUDP), true); }
            }
        }

        // Receive TCP
        if(TCP.available() > 0)
        {
            unsigned int PacketLen = TCP.available();
            byte* Packet = new byte[PacketLen];
            for(int i = 0; i < PacketLen; i++) { Packet[i] = TCP.read(); }
            if(TRACE_LOGGING)
            {
                Serial.print("Received TCP packet of size ");
                Serial.print(PacketLen);
                Serial.print(" from ");
                IPAddress RemoteIP = TCP.remoteIP();
                for(int i = 0; i < 4; i++)
                {
                    Serial.print(RemoteIP[i], DEC);
                    if(i < 3) { Serial.print('.'); }
                }
                Serial.print(':');
                Serial.println(TCP.remotePort());
                Serial.print("Contents: ");
                printHexArray(Packet, PacketLen);
                Serial.println();
            }
            if(PacketLen >= 11)
            {
                short ExpectedPacketLength = (Packet[9] << 8) | Packet[10];
                if(ExpectedPacketLength != PacketSizeUDP) // Size mismatch.
                {
                    Serial.print("TCP packet expected length of ");
                    Serial.print(ExpectedPacketLength, DEC);
                    Serial.print(" does not match received length ");
                    Serial.print(PacketSizeUDP, DEC);
                    Serial.println(" and will be ignored.");
                }
                else if(PacketHandlers[Packet[8]] == nullptr) { handleUnknown(Packet, false); }
                else { (*PacketHandlers[Packet[8]])(Packet, false); }
            }
        }
        delay(20);
    }
    while(LastWatchdog + WatchdogTimeout > millis() && TCP.connected());

    Serial.println("Disconnected from server, attempting to reconnect in 5s...");
    delay(5000);
}

void sendPacketTCP(byte* Packet, int Length)
{
    if(Length < 11) { Serial.println("Cannot send TCP packet of size less than 11."); return; } // Packets must be at least 11 bytes (header). Any smaller are discarded, as they are invalid.
    if(TCP.connected())
    {
        TCP.write(Packet, Length);
    }
}

void sendPacketUDP(byte* Packet, int Length)
{
    if(Length < 11) { Serial.println("Cannot send UDP packet of size less than 11."); return; } // Packets must be at least 11 bytes (header). Any smaller are discarded, as they are invalid.
    if(TRACE_LOGGING)
    {
        Serial.print("Sending UDP packet of length ");
        Serial.print(Length, DEC);
        Serial.print(", contents ");
        printHexArray(Packet, Length);
        Serial.println();
    }
    UDP.beginPacket(ServerAddress, RemotePortUDP);
    UDP.write(Packet, Length);
    UDP.endPacket();
}

void handleWatchdog(byte* Packet, bool IsUDP)
{
    if(TRACE_LOGGING) { Serial.println("Handling watchdog packet."); }
    if(!IsUDP) { Serial.println("Received TCP watchdog packet, ignoring."); return; }
    byte WatchdogResponse[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp placeholder
                                0xF1, // Packet ID (WATCHDOG_FROM_CLIENT)
                                0, 11}; // Length
    addTimestamp(WatchdogResponse);
    sendPacketUDP(WatchdogResponse, sizeof(WatchdogResponse));
    LastWatchdog = millis();
}

void handleInvalid(byte* Packet, bool IsUDP)
{
    Serial.print("Received invalid ");
    if(IsUDP) { Serial.print("UDP"); }
    else { Serial.print("TCP"); }
    Serial.print(" packet with contents ");
    printHexArray(Packet, sizeof(Packet));
    Serial.println();
}

void handleUnknown(byte* Packet, bool IsUDP)
{
    Serial.print("Received unknown ");
    if(IsUDP) { Serial.print("UDP"); }
    else { Serial.print("TCP"); }
    Serial.print(" packet with contents ");
    printHexArray(Packet, sizeof(Packet));
    Serial.println();
}

void printHexArray(char* Array, int Length)
{
    Serial.print("[0x");
    for(int i = 0; i < Length; i++)
    {
        if(Array[i] < 0x10) { Serial.print('0'); }
        Serial.print(Array[i], HEX);
        if(i < (Length - 1)) { Serial.print(' '); }
    }
    Serial.print(']');
}

void printHexArray(byte* Array, int Length)
{
    Serial.print("[0x");
    for(int i = 0; i < Length; i++)
    {
        if(Array[i] < 0x10) { Serial.print('0'); }
        Serial.print(Array[i], HEX);
        if(i < (Length - 1)) { Serial.print(' '); }
    }
    Serial.print(']');
}

byte* subArray(char* Array, int Length)
{
    byte* Output = new byte[Length];
    for(int i = 0; i < Length; i++) { Output[i] = Array[i]; }
    return Output;
}

void addTimestamp(byte* Packet)
{
    // TODO: Determine how to get the actual time. Or rely on the server to synchronize us?
    unsigned long long Timestamp = millis();
    Timestamp *= 10000;
    for(int i = 0; i < 8; i++)
    {
        Packet[i] = (Timestamp >> ((7 - i) * 8)) & 0xFF;
    }
}