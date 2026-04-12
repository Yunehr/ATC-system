/**
 * @file PacketTransport.hpp
 * @brief Static transport layer for sending and receiving @c packet objects over TCP.
 *
 * Abstracts the Winsock @c send() / @c recv() calls behind a reliable
 * all-or-nothing interface. Callers work entirely in terms of @c packet
 * objects and never interact with the raw socket API directly.
 *
 * @note All methods are static — @c PacketTransport is a utility class
 *       and is not intended to be instantiated.
 *
 * @see PacketTransport.cpp for implementation details.
 * @see packet.h for wire format and @c packet internals.
 */
#pragma once
#include "packet.h"

class PacketTransport {
public:
    /**
     * @brief Serializes @p pak and sends all bytes over @p sock.
     *
     * @param sock  Connected TCP socket descriptor.
     * @param pak   Packet to serialize and transmit.
     * @return @c true on success; @c false if serialization fails or
     *         the socket write is interrupted or closed.
     * @see sendAll()
     */
    static bool sendPacket(int sock, packet& pak);

    /**
     * @brief Receives one complete packet from @p sock and reconstructs it.
     *
     * Performs a two-phase read: first the fixed-size header to determine
     * payload length, then the remaining body and CRC tail. The result is
     * written into @p outpak via deep copy.
     *
     * @param sock    Connected TCP socket descriptor.
     * @param outpak  Output parameter set to the received packet on success.
     *                Left unchanged on failure.
     * @return @c true on success; @c false if either read phase fails.
     * @see recvAll()
     */
    static bool receivePacket(int sock, packet& outpak);

private:
       /**
     * @brief Sends exactly @p size bytes, retrying until the full buffer
     *        is transmitted or an error occurs.
     *
     * @param sock  Connected TCP socket descriptor.
     * @param data  Buffer to send.
     * @param size  Number of bytes to send from @p data.
     * @return @c true if all @p size bytes were sent;
     *         @c false if @c send() returns 0 or -1.
     */
    static bool sendAll(int sock, const char* data, int size);

     /**
     * @brief Receives exactly @p size bytes, retrying until the full count
     *        is read or an error occurs.
     *
     * @param sock    Connected TCP socket descriptor.
     * @param buffer  Destination buffer; must be at least @p size bytes.
     * @param size    Exact number of bytes to read.
     * @return @c true if exactly @p size bytes were received;
     *         @c false if @c recv() returns 0 or -1.
     */
    static bool recvAll(int sock, char* buffer, int size);
};