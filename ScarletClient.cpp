#include "Arduino.h"
#include "ScarletClient.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

ScarletClient::ScarletClient(char* ClientName, unsigned int LocalPortUDP, IPAddress Server, int PortTCP, int PortUDP, byte DebugMode)
{
    _ClientName = ClientName;
    _LocalPortUDP = LocalPortUDP;
    _ServerAddress = Server;
    _RemotePortTCP = PortTCP;
    _RemotePortUDP = PortUDP;
    _DebugMode = DebugMode;

    PacketHandlers[0xF0] = ScarletClient::HandleWatchdog; // WATCHDOG_FROM_SERVER
    PacketHandlers[0xF1] = HandleInvalid; // WATCHDOG_FROM_CLIENT, should never be received by another client.
    // TODO: Implement remaining packet handlers.
    //PacketHandlers[0xF2] = HandlePacketBufferResize; // BUFFER_LENGTH_CHANGE
    //PacketHandlers[0xF3] = HandleTimeSync; // TIME_SYNCHRONIZATION
    PacketHandlers[0xF4] = HandleInvalid; // HANDSHAKE_FROM_CLIENT, should never be received by another client.
    PacketHandlers[0xF5] = HandleInvalid; // HANDSHAKE_FROM_SERVER, should not be received outside of the connection building block, so is ignored otherwise.
}

void ScarletClient::SetWatchdogTimeout(unsigned int WatchdogTimeout) { _WatchdogTimeout = WatchdogTimeout; }

// Main methods.
bool ScarletClient::Connect()
{
    // Begin TCP connection.
    DEBUG(Serial.println("=====");)
    DEBUG(Serial.println("Attempting server connections...");)

    if(!TCP.connect(_ServerAddress, _RemotePortTCP))
    {
        DEBUG(Serial.println("Server TCP connection failed, retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }

    // Prepare a handshake packet.
    unsigned short HandshakeLen = 15 + (strlen(_ClientName) * 2);
    TRACE
    (
        Serial.print("Length of name is ");
        Serial.print((int)strlen(_ClientName), DEC);
        Serial.print(" so hanshake packet size will be ");
        Serial.println(HandshakeLen, DEC);
    )

    byte* HandshakeTCP = new byte[HandshakeLen] {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp (added after)
                        0xF4, // Packet ID (HANDSHAKE_FROM_CLIENT)
                        ((HandshakeLen >> 8) & 0xFF), (HandshakeLen & 0xFF), // Length
                        0x00, // Latency Management (NONE)
                        0xC0, // Version
                        ((_LocalPortUDP >> 8) & 0xFF), (_LocalPortUDP & 0xFF) // Local UDP port
                        }; // Name (added after)

    AddTimestamp(HandshakeTCP);
    for(int i = 0; i < strlen(_ClientName); i++) // Add name
    {
        HandshakeTCP[15 + (i * 2)] = (_ClientName[i] >> 8) & 0xFF;
        HandshakeTCP[16 + (i * 2)] = _ClientName[i] & 0xFF;
    }

    // Send the handshake packet.
    if(TCP.connected())
    {
        DEBUG(Serial.println("Sending handshake to server...");)
        TCP.write(HandshakeTCP, HandshakeLen);
        DEBUG(Serial.println("Handshake sent.");)
    }

    // Wait for handshake response.
    unsigned long HandshakeWaitStart = millis();
    while(TCP.available() == 0)
    {
        if(millis() - HandshakeWaitStart > 5000)
        {
            DEBUG(Serial.println("Did not receive handshake response.");)
            TCP.stop();
            TimeoutActive = true;
            TimeoutEnd = millis() + 5000;
            return false;
        }
    }

    // Receive handshake response.
    unsigned int HandshakeRespLen = TCP.available();
    byte* HandshakeRespBuffer = new byte[HandshakeRespLen];
    for(int i = 0; i < HandshakeRespLen; i++) { HandshakeRespBuffer[i] = TCP.read(); }
    TRACE(Serial.print("Received handshake response from server of length ");)
    TRACE(Serial.println(HandshakeRespLen);)

    if(HandshakeRespLen < 13)
    {
        DEBUG(Serial.println("Handshake response length inadequate. Retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }

    if(HandshakeRespBuffer[8] != 0xF5) // Packet ID (HANDSHAKE_FROM_SERVER)
    {
        DEBUG(Serial.print("Expected handshake response, got ID 0x");)
        DEBUG(Serial.print(HandshakeRespBuffer[8], HEX);)
        DEBUG(Serial.println(" instead. Retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }
    
    short ExpectedPacketLength = (HandshakeRespBuffer[9] << 8) | HandshakeRespBuffer[10];
    if(ExpectedPacketLength != HandshakeRespLen) // Size mismatch.
    {
        DEBUG
        (
            Serial.print("Handshake expected length of ");
            Serial.print(ExpectedPacketLength, DEC);
            Serial.print(" does not match received length ");
            Serial.print(HandshakeRespLen, DEC);
            Serial.println(". Retrying in 5s...");
        )
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }

    // Interpret handshake response.
    DEBUG(Serial.print("Server is on Scarlet version 0x");)
    DEBUG(Serial.print(HandshakeRespBuffer[11], HEX);) // Server version
    DEBUG(Serial.println('.');)

    // For future use if version incompability gets introduced.
    /*if(HandshakeRespBuffer[11] > 0x10) // Replace with criteria for incompatible version(s).
    {
        DEBUG(Serial.println("This version is not compatible. Retrying in 60s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 60000;
        return;
    }*/

    if(HandshakeRespBuffer[12] == 0x00) // Connection result (OKAY)
    {
        DEBUG(Serial.println("TCP client successfully connected!");)
    }
    else if(HandshakeRespBuffer[12] == 0x01) // Connection result (INVALID_NAME)
    {
        DEBUG(Serial.println("Server rejected client name. Retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }
    else if(HandshakeRespBuffer[12] == 0x02) // Connection result (INCOMPATIBLE_VERSIONS)
    {
        DEBUG(Serial.println("Server rejected client due to incompatible versions. Retrying in 60s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 60000;
        return false;
    }
    else if(HandshakeRespBuffer[12] == 0x03) // Connection result (CONNECTION_FAILED)
    {
        DEBUG(Serial.println("Server returned a general error. Retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }
    else // Connection result unknown
    {
        DEBUG(Serial.println("Server returned an unknown handshake response type. Retrying in 5s...");)
        TimeoutActive = true;
        TimeoutEnd = millis() + 5000;
        return false;
    }
    // TCP connection is now complete.

    UDP.begin(_LocalPortUDP); // Listen for incoming packets.
    
    LastWatchdog = millis(); // Prepare watchdog timer

    return true;
}

void ScarletClient::Tick()
{
    // Check if we are connected first.

    if(TimeoutActive && TimeoutEnd < millis()) { TimeoutActive = false; } // The timeout was active, but the timer has run out.
    else if(TimeoutActive) { return; } // The timeout is still active.
    
    if(IsConnected) // If we were already connected, make sure the connection is still OK.
    {
        IsConnected = LastWatchdog + _WatchdogTimeout > millis() && TCP.connected();
        if(!IsConnected) // Connection was lost.
        {
            DEBUG(Serial.println("Disconnected from server, attempting to reconnect in 5s...");)
            TimeoutActive = true;
            TimeoutEnd = millis() + 5000;
            return;
        }
    }

    if(!IsConnected) { IsConnected = Connect(); return; } // Attempt to connect. Check results, and possibly retry on next tick.
    
    // We are connected, so let's send/receive.

    // Receive UDP
    int PacketSizeUDP = UDP.parsePacket();
    if(PacketSizeUDP > 0) // We received a UDP packet
    {
        TRACE
        (
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
        )
        UDP.read(PacketBufferUDP, UDP_TX_PACKET_MAX_SIZE);
        TRACE(Serial.print("Contents: ");)
        TRACE(PrintHexArray(PacketBufferUDP, PacketSizeUDP);)
        TRACE(Serial.println();)
        if(PacketSizeUDP >= 11) // Valid packet.
        {
            short ExpectedPacketLength = (PacketBufferUDP[9] << 8) | PacketBufferUDP[10];
            if(ExpectedPacketLength != PacketSizeUDP) // Size mismatch.
            {
                DEBUG
                (
                    Serial.print("UDP packet expected length of ");
                    Serial.print(ExpectedPacketLength, DEC);
                    Serial.print(" does not match received length ");
                    Serial.print(PacketSizeUDP, DEC);
                    Serial.println(" and will be ignored.");
                )
            }
            else if(PacketHandlers[PacketBufferUDP[8]] == nullptr) { handleUnknown(SubArray(PacketBufferUDP, PacketSizeUDP), true); }
            else { (*PacketHandlers[PacketBufferUDP[8]])(SubArray(PacketBufferUDP, PacketSizeUDP), true); }
        }
    }

    // Receive TCP
    if(TCP.available() > 0)
    {
        unsigned int PacketLen = TCP.available();
        byte* Packet = new byte[PacketLen];
        for(int i = 0; i < PacketLen; i++) { Packet[i] = TCP.read(); }
        TRACE
        (
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
            PrintHexArray(Packet, PacketLen);
            Serial.println();
        )
        if(PacketLen >= 11)
        {
            short ExpectedPacketLength = (Packet[9] << 8) | Packet[10];
            if(ExpectedPacketLength != PacketSizeUDP) // Size mismatch.
            {
                DEBUG
                (
                    Serial.print("TCP packet expected length of ");
                    Serial.print(ExpectedPacketLength, DEC);
                    Serial.print(" does not match received length ");
                    Serial.print(PacketSizeUDP, DEC);
                    Serial.println(" and will be ignored.");
                )
            }
            else if(PacketHandlers[Packet[8]] == nullptr) { handleUnknown(Packet, false); }
            else { (*PacketHandlers[Packet[8]])(Packet, false); }
        }
    }
}

// Packet sending
void ScarletClient::SendPacketTCP(byte* Packet, unsigned int Length)
{
    if(Length < 11) { DEBUG(Serial.println("Cannot send TCP packet of size less than 11.");) return; } // Packets must be at least 11 bytes (header). Any smaller are discarded, as they are invalid.
    if(TCP.connected()) { TCP.write(Packet, Length); }
}

void ScarletClient::SendPacketUDP(byte* Packet, unsigned int Length)
{
    if(Length < 11) { DEBUG(Serial.println("Cannot send UDP packet of size less than 11.");) return; } // Packets must be at least 11 bytes (header). Any smaller are discarded, as they are invalid.
    TRACE
    (
        Serial.print("Sending UDP packet of length ");
        Serial.print(Length, DEC);
        Serial.print(", contents ");
        PrintHexArray(Packet, Length);
        Serial.println();
    )
    UDP.beginPacket(_ServerAddress, _RemotePortUDP);
    UDP.write(Packet, Length);
    UDP.endPacket();
}

// Packet handling
void ScarletClient::HandleWatchdog(byte* Packet, bool IsUDP)
{
    TRACE(Serial.println("Handling watchdog packet.");)
    if(!IsUDP) { DEBUG(Serial.println("Received TCP watchdog packet, ignoring.");) return; }
    byte WatchdogResponse[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp placeholder
                                0xF1, // Packet ID (WATCHDOG_FROM_CLIENT)
                                0, 11}; // Length
    AddTimestamp(WatchdogResponse);
    SendPacketUDP(WatchdogResponse, sizeof(WatchdogResponse));
    LastWatchdog = millis();
}

void ScarletClient::HandleInvalid(byte* Packet, bool IsUDP)
{
    DEBUG
    (
        Serial.print("Received invalid ");
        if(IsUDP) { Serial.print("UDP"); }
        else { Serial.print("TCP"); }
        Serial.print(" packet with contents ");
        PrintHexArray(Packet, sizeof(Packet));
        Serial.println();
    )
}

void ScarletClient::HandleUnknown(byte* Packet, bool IsUDP)
{
    DEBUG
    (
        Serial.print("Received unknown ");
        if(IsUDP) { Serial.print("UDP"); }
        else { Serial.print("TCP"); }
        Serial.print(" packet with contents ");
        PrintHexArray(Packet, sizeof(Packet));
        Serial.println();
    )
}

// Utilities
void ScarletClient::AddTimestamp(byte* Packet)
{
    // TODO: Determine how to get the actual time. Or rely on the server to synchronize us?
    unsigned long long Timestamp = millis();
    Timestamp *= 10000;
    for(int i = 0; i < 8; i++) { Packet[i] = (Timestamp >> ((7 - i) * 8)) & 0xFF; }
}

void ScarletClient::PrintHexArray(char* Array, int Length)
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

void ScarletClient::PrintHexArray(byte* Array, int Length)
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

byte* ScarletClient::SubArray(char* Array, int Length)
{
    byte* Output = new byte[Length];
    for(int i = 0; i < Length; i++) { Output[i] = Array[i]; }
    return Output;
}