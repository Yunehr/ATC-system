/**
 * @file PacketTransport.cpp
 * @brief Winsock send/receive helpers that frame @c packet objects over TCP.
 *
 * Bridges the @c packet serialization layer and the raw Winsock socket API.
 * All four functions guarantee that either the full requested byte count is
 * transferred or @c false is returned — partial transfers are handled
 * internally by the @c sendAll / @c recvAll loop pair.
 *
 * ### Wire format assumed by @c receivePacket
 * @code
 * [ header (2 bytes) | payload (0–249 bytes) | CRC (4 bytes) ]
 * @endcode
 * The payload length is read from byte 1 of the header, matching the
 * @c packet::header layout defined in @c packet.h.
 */
#include "PacketTransport.hpp"
#include <winsock2.h>
#include <cstring>
/**
 * @brief Serializes a packet and sends all bytes over a TCP socket.
 *
 * Calls @c packet::Serialize() to obtain the wire buffer, then delegates
 * to @c sendAll() to guarantee the full buffer is transmitted.
 *
 * @param sock  Connected Winsock socket descriptor.
 * @param pak   The packet to serialize and send. The internal @c txbuff
 *              remains owned by @p pak — do not free it externally.
 * @return @c true if all bytes were sent successfully;
 *         @c false if serialization produced a null or zero-length buffer,
 *         or if the underlying @c send() call failed.
 */
bool PacketTransport::sendPacket(int sock, packet& pak)
{
    int size = 0;
    char* buffer = pak.Serialize(&size);

    if (buffer == nullptr || size <= 0) {
        return false;
    }

    return sendAll(sock, buffer, size);
}
/**
 * @brief Receives exactly one packet from a TCP socket into @p outpak.
 *
 * Uses a two-phase read to avoid buffering the entire stream:
 *  -# Read the fixed 2-byte header to determine payload length.
 *  -# Allocate a full-packet buffer, copy the header in, then read the
 *     remaining body and CRC tail directly into it.
 *
 * The completed buffer is passed to the @c packet(char*) constructor and
 * the result is assigned to @p outpak via @c operator=, which performs a
 * deep copy. The temporary buffer is freed before returning.
 *
 * @param sock    Connected Winsock socket descriptor.
 * @param outpak  Output parameter populated with the received packet on success.
 *                Unchanged on failure.
 * @return @c true if a complete, well-formed packet was received;
 *         @c false if either @c recvAll() call fails (socket closed,
 *         connection reset, or other Winsock error).
 *
 * @warning The header size is hardcoded as 2 bytes to match the current
 *          @c packet::header bitfield layout. If that struct changes,
 *          this value must be updated to match. Consider exposing a
 *          @c packet::HEADER_SIZE constant to keep them in sync.
 *
 * @warning Payload length is read directly from the raw header byte with
 *          no validation against @c MAX_PKTSIZE. A malformed or malicious
 *          header could cause an oversized allocation.
 */
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
/**
 * @brief Sends exactly @p size bytes over a socket, looping until complete.
 *
 * TCP's @c send() is not guaranteed to transmit all requested bytes in a
 * single call. This function retries from the last written position until
 * the full @p size is confirmed sent.
 *
 * @param sock  Connected Winsock socket descriptor.
 * @param data  Pointer to the buffer to send.
 * @param size  Number of bytes to send from @p data.
 * @return @c true if all @p size bytes were sent;
 *         @c false if @c send() returns 0 (connection closed) or -1 (error).
 */
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
/**
 * @brief Receives exactly @p size bytes from a socket, looping until complete.
 *
 * TCP's @c recv() may return fewer bytes than requested if the data has not
 * yet arrived. This function retries from the last read position until
 * the full @p size is received.
 *
 * @param sock    Connected Winsock socket descriptor.
 * @param buffer  Destination buffer. Must be at least @p size bytes.
 * @param size    Exact number of bytes to receive.
 * @return @c true if exactly @p size bytes were read into @p buffer;
 *         @c false if @c recv() returns 0 (connection closed) or -1 (error).
 */
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