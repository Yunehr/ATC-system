/**
 * @file ClientEngine.hpp
 * @brief Declaration of the ClientEngine class.
 *
 * ClientEngine is the low-level networking layer of the aviation client.  It
 * owns the Winsock TCP socket and implements three request patterns that map
 * directly onto the shared packet protocol:
 *
 * | Method                 | Outgoing packet         | Use case                        |
 * |------------------------|-------------------------|---------------------------------|
 * | dataRequest()          | @c pkt_req + Request    | Weather, flight plan, telemetry |
 * | pkRequest()            | Raw typed packet        | Auth, emergency, resolve        |
 * | downloadFlightManual() | @c pkt_req + req_file   | Multi-packet file transfer      |
 *
 * All methods perform CRC verification on received packets and return
 * human-readable error strings on failure, which are forwarded to the
 * Python UI via the stdout bridge in main.cpp.
 */
#pragma once
#include <winsock2.h>
#include <string>
#include "../shared/Packet.h"

/**
 * @class ClientEngine
 * @brief Low-level TCP socket wrapper and packet-protocol implementation.
 *
 * Responsibilities:
 * - Opening and closing a TCP socket connection to the backend server.
 * - Serialising outgoing requests into the shared packet format.
 * - Receiving, CRC-verifying, and unpacking server response packets.
 * - Managing a multi-packet file transfer for the flight manual download.
 *
 * ClientEngine is owned by ClientApp, which handles Winsock initialisation
 * (WSAStartup / WSACleanup) before constructing this object.
 */
class ClientEngine {
public:

    /**
    * @brief Constructs a ClientEngine with an invalid socket and a default client ID.
    *
    * The socket is not opened until connectToServer() is called.
    */
    ClientEngine();

    /**
     * @brief Destructor — calls disconnect() to ensure the socket is closed.
     */    
    ~ClientEngine();

    // -------------------------------------------------------------------------
    // Connection management
    // -------------------------------------------------------------------------
 
    /**
    * @brief Opens a TCP connection to the specified server.
    *
    * Creates an AF_INET/SOCK_STREAM socket and calls connect().  On failure
    * the socket is closed and reset to INVALID_SOCKET so that subsequent
    * guard checks in callers behave correctly.
    *
    * @param ip   IPv4 address of the server as a dotted-decimal string.
    * @param port TCP port to connect to.
    * @return @c true  if the connection was established successfully.
    * @return @c false if socket creation or connect() failed.
    */
    bool connectToServer(const std::string& ip, unsigned short port);
    /**
    * @brief Gracefully closes the active socket connection.
    *
    * Safe to call when the socket is already invalid (no-op in that case).
    * Resets @ref sock to INVALID_SOCKET after closing.
    */
    void disconnect();

    // -------------------------------------------------------------------------
    // Request methods
    // -------------------------------------------------------------------------
 
    /**
    * @brief Sends a typed data request to the server and returns the response.
    *
    * Serialises @p data into a Request of the given @p type, wraps it in a
    * @c pkt_req packet, and transmits it via PacketTransport.  The response
    * packet is CRC-verified before its payload is returned.
    *
    * @param data Payload to include in the request (may be empty).
    * @param type Request type enumeration value (e.g. @c req_weather, @c req_fplan).
    * @return The server's response payload string, or one of:
    *         @c "Failed to send request",
    *         @c "Failed to receive response",
    *         @c "CRC Checksum Failed",
    *         @c "Received incomplete response",
    *         @c "Unexpected response type".
    */    
    std::string dataRequest(const std::string& data, reqtyp type);
    /**
    * @brief Sends a raw typed packet to the server and returns the response.
    *
    * Unlike dataRequest(), the payload is placed directly into the packet
    * without wrapping it in a Request struct.  The response packet type must
    * match @p PKtype for the payload to be returned.
    *
    * @param data   Payload bytes to include in the outgoing packet (may be empty).
    * @param PKtype Packet type flag for both the outgoing packet and the
    *               expected response (e.g. @c pkt_auth, @c pkt_emgcy).
    * @return The server's response payload string, or one of:
    *         @c "Failed to send request",
    *         @c "Failed to receive response",
    *         @c "CRC Checksum Failed",
    *         @c "Received incomplete response",
    *         @c "Unexpected response type".
    */
    std::string pkRequest(const std::string&data, pkTyFl PKtype);
    /**
    * @brief Downloads the flight manual from the server and writes it to disk.
    *
    * Sends a @c req_file request and handles the subsequent multi-packet file
    * transfer via FileReceiver, reporting progress to stdout.  If
    * FileReceiver::start() fails, incoming packets are drained to keep the
    * socket in a consistent state before the error is returned.
    *
    * @param outputPath Filesystem path where the downloaded PDF should be saved
    *                   (e.g. @c "../client/flightmanual.pdf").
    * @return @c "Flight Manual downloaded to <outputPath>" on success, or an
    *         error description string on failure.
    */    
    std::string downloadFlightManual(const std::string& outputPath);

private:
    /// @brief The active TCP socket, or INVALID_SOCKET when not connected.
    SOCKET sock;
    /// @brief Client identifier stamped into the header of every outgoing packet.
    unsigned int clientID;
};