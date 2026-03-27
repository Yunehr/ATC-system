#include "PacketTransport.hpp"
#include <winsock2.h>
#include <cstring>

bool PacketTransport::sendPacket(int sock, packet& pak)
{
    int size = 0;
    char* buffer = pak.Serialize(&size);

    if (buffer == nullptr || size <= 0) {
        return false;
    }

    return sendAll(sock, buffer, size);
}

bool PacketTransport::receivePacket(int sock, packet& outpak)
{
    // Use sizeof(HEAD) instead of hardcoded '2' for reliability
    const int HEADER_SIZE = 2; 
    char headerBuff[HEADER_SIZE];

    if (!recvAll(sock, headerBuff, HEADER_SIZE)) {
        return false;
    }

    // Extract length from the buffer
    uint8_t payloadLength = (uint8_t)headerBuff[1];
    int totalSize = HEADER_SIZE + payloadLength + sizeof(int32_t); 

    char* fullBuffer = new char[totalSize];
    memcpy(fullBuffer, headerBuff, HEADER_SIZE);

    // Get the rest of the packet (Body + Tail)
    if (!recvAll(sock, fullBuffer + HEADER_SIZE, totalSize - HEADER_SIZE)) {
        delete[] fullBuffer;
        return false;
    }

    // This now uses the 'operator=' for a safe deep copy
    outpak = packet(fullBuffer); 

    delete[] fullBuffer;
    return true;
}

bool PacketTransport::sendAll(int sock, const char* data, int size)
{
    int totalSent = 0;

    while (totalSent < size) {
        int sent = send(sock, data + totalSent, size - totalSent, 0);

        if (sent <= 0) {
            return false;
        }

        totalSent += sent;
    }

    return true;
}

bool PacketTransport::recvAll(int sock, char* buffer, int size)
{
    int totalReceived = 0;

    while (totalReceived < size) {
        int received = recv(sock, buffer + totalReceived, size - totalReceived, 0);

        if (received <= 0) {
            return false;
        }

        totalReceived += received;
    }

    return true;
}