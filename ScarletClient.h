/*
    Basic Scarlet Client implementation for use on the ESP8266 running the Arduino core.
    Most basic functionality is implemented (watchdogs, UDP and TCP send/receive etc.)
    Some advanced features are not implemented (time synchronization, client-client communication, connection quality monitoring, etc.)

    Written by Cai Biesinger, GitHub: @CaiB
    Released under LGPLv3.
*/
#ifndef ScarletClient_h
#define ScarletClient_h

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#define DEBUG(x) if(_DebugMode > 0) { x }
#define TRACE(x) if (_DebugMode > 1) { x }

class ScarletClient
{
    public:
        ScarletClient(char* ClientName, unsigned int LocalPortUDP, IPAddress Server, int PortTCP, int PortUDP, byte DEBUGMode);
        typedef void (*PacketHandlerFxn)(byte* Packet, bool IsUDP);
        void Tick(); // Should be run as often as possible. At least 10Hz recommended to avoid falling behind on packet receiving.
        void SendPacketTCP(byte* Packet, unsigned int Length);
        void SendPacketUDP(byte* Packet, unsigned int Length);
        void AddPacketHandler(byte ID, PacketHandlerFxn Handler);
        void SetWatchdogTimeout(unsigned int WatchdogTimeout);
    private:
        typedef void (ScarletClient::*PacketHandlerFxnInt)(byte* Packet, bool IsUDP); // Janky AF. Why, C++, why?
        PacketHandlerFxn PacketHandlers[0xEF];
        PacketHandlerFxnInt PacketHandlersInt[0x10];
        void AddPacketHandlerInt(byte PacketID, PacketHandlerFxnInt Handler);

        bool Connect();
        void AddTimestamp(byte* Packet);
        void PrintHexArray(char* Array, int Length);
        void PrintHexArray(byte* Array, int Length);
        byte* SubArray(char* Array, int Length);

        void HandleWatchdog(byte* Packet, bool IsUDP);
        void HandleInvalid(byte* Packet, bool IsUDP);
        void HandleUnknown(byte* Packet, bool IsUDP);

        byte _DebugMode; // 0 = None, 1 = Basic serial, 2 = TraceLogging via Serial.

        unsigned int _LocalPortUDP;
        unsigned int _WatchdogTimeout = 5000;

        char* _ClientName;
        IPAddress _ServerAddress;
        int _RemotePortTCP;
        int _RemotePortUDP;

        char PacketBufferUDP[UDP_TX_PACKET_MAX_SIZE];
        char ReplyBufferUDP[UDP_TX_PACKET_MAX_SIZE];
        unsigned long LastWatchdog = 0;

        WiFiUDP UDP;
        WiFiClient TCP;

        unsigned long LastTimer;
        bool TimeoutActive;
        unsigned long TimeoutEnd;
        bool IsConnected = false;
};

#endif