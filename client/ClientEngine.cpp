/**
 * @file ClientEngine.cpp
 * @brief Implementation of the ClientEngine class.
 *
 * Provides TCP connectivity to the backend server and implements three
 * transport-level request patterns:
 * - dataRequest()  — wraps a payload in a Request struct before sending.
 * - pkRequest()    — sends a raw packet of a specified type.
 * - downloadFlightManual() — multi-packet file transfer with progress reporting.
 *
 * All methods perform CRC verification on received packets and map transport
 * errors to human-readable strings that are forwarded back to the Python UI
 * via the stdout bridge.
 */
#include "ClientEngine.hpp"
#include "../shared/PacketTransport.hpp"
#include "../shared/Packet.h"
#include "../shared/Request.h"
#include "FileReceiver.hpp"
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <vector>

// ---------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------
 
/**
 * @brief Constructs a ClientEngine with an invalid socket and a default client ID.
 *
 * The socket is not opened until connectToServer() is called.
 */
ClientEngine::ClientEngine() : sock(INVALID_SOCKET), clientID(1) {}
 
/**
 * @brief Destructor — ensures the socket is closed before the object is destroyed.
 */
ClientEngine::~ClientEngine() {
    disconnect();
}

// ---------------------------------------------------------
// Connection management
// ---------------------------------------------------------
 
/**
 * @brief Opens a TCP connection to the specified server.
 *
 * Creates an AF_INET/SOCK_STREAM socket, resolves the IPv4 address, and
 * calls connect().  On failure the socket is closed and reset to
 * INVALID_SOCKET so that subsequent guard checks in the callers work
 * correctly.
 *
 * @param ip   IPv4 address of the server as a dotted-decimal string.
 * @param port TCP port to connect to.
 * @return @c true  if the connection was established successfully.
 * @return @c false if socket creation or connect() failed.
 */
bool ClientEngine::connectToServer(const std::string& ip, unsigned short port) {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cout << "socket() failed\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "connect() failed\n";
        closesocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }

    std::cout << "Connected to server!\n";
    return true;
}

/**
 * @brief Gracefully closes the active socket connection.
 *
 * Safe to call when the socket is already invalid (no-op in that case).
 * Resets @ref sock to INVALID_SOCKET after closing.
 */
void ClientEngine::disconnect() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

// ---------------------------------------------------------
// Request handlers
// ---------------------------------------------------------
 
/**
 * @brief Sends a typed data request to the server and returns the response.
 *
 * Serialises @p data into a Request of the given @p type, wraps it in a
 * @c pkt_req packet, and transmits it via PacketTransport.  The response
 * packet is then CRC-verified and its payload is returned as a string.
 *
 * Error handling (each condition returns a descriptive string):
 * - Send failure            → @c "Failed to send request"
 * - Receive failure         → @c "Failed to receive response"
 * - CRC mismatch            → @c "CRC Checksum Failed"
 * - Incomplete transmission → @c "Received incomplete response"
 * - Unexpected packet type  → @c "Unexpected response type"
 *
 * @param data Payload to include in the request (may be empty).
 * @param type The request type enumeration value (e.g. @c req_weather, @c req_fplan).
 * @return The server's response payload string, or an error description.
 */
std::string ClientEngine::dataRequest(const std::string& data, reqtyp type) {
    
    int reqSize = data.size();   //-1 for null terminator used in testing, Unknown if needed here
    Request txReq(type, reqSize, (char*)data.data());
    
    int reqSerialSize = 0;
    char* serializedReq = txReq.serializeRequest(&reqSerialSize);


    // Create packet body (e.g., location string)
    packet txPkt;
    txPkt.PopulPacket(serializedReq, reqSerialSize, (char)clientID, pkt_req);
    delete[] serializedReq;

    Logger::logPacket(txPkt, Logger::Direction::TX, "Data request type " + std::to_string((int)type));
    if (!PacketTransport::sendPacket((int)sock, txPkt)) {
        return "Failed to send request";
    }

    packet rxPkt;
    if (!PacketTransport::receivePacket((int)sock, rxPkt)) {
        return "Failed to receive response";
    }

    Logger::logPacket(rxPkt, Logger::Direction::RX, "Response to request type " + std::to_string((int)type));

    //verify crc
    if (rxPkt.calcCRC() != rxPkt.GetCRC()){
        return "CRC Checksum Failed";
    }

    //read header to determine if response is valid and what type it is
    if (rxPkt.getTFlag() == PKT_TRNSMT_INCOMP) {
        //std::cout << "Received incomplete packet\n";
        return "Received incomplete response";
    }
    if (rxPkt.getPKType() == pkt_dat) {
        return std::string(rxPkt.getData(), rxPkt.getPloadLength());
    }

    return "Unexpected response type";
}

/**
 * @brief Sends a raw typed packet to the server and returns the response.
 *
 * Unlike dataRequest(), the payload is placed directly into the packet
 * without first wrapping it in a Request struct.  The response packet type
 * must match @p PKType for the payload to be returned; any other type is
 * treated as an error.
 *
 * Error handling (each condition returns a descriptive string):
 * - Send failure            → @c "Failed to send request"
 * - Receive failure         → @c "Failed to receive response"
 * - CRC mismatch            → @c "CRC Checksum Failed"
 * - Incomplete transmission → @c "Received incomplete response"
 * - Type mismatch           → @c "Unexpected response type"
 *
 * @param data    Payload bytes to include in the outgoing packet (may be empty).
 * @param PKType  The packet type flag for both the outgoing and expected incoming packet.
 * @return The server's response payload string, or an error description.
 */
std::string ClientEngine::pkRequest(const std::string&data, pkTyFl PKType){
    packet txPkt;
    int size = data.size();
    txPkt.PopulPacket((char*)data.data(),size,(char)clientID,PKType);

    Logger::logPacket(txPkt, Logger::Direction::TX, "Packet request type " + std::to_string((int)PKType));
    if (!PacketTransport::sendPacket((int)sock, txPkt)) {
        return "Failed to send request";
    }

    packet rxPkt;
    if (!PacketTransport::receivePacket((int)sock, rxPkt)) {
        return "Failed to receive response";
    }

    Logger::logPacket(rxPkt, Logger::Direction::RX, "Response to packet type " + std::to_string((int)PKType));

    //verify crc
    if (rxPkt.calcCRC() != rxPkt.GetCRC()){
        return "CRC Checksum Failed";
    }

    //read header to determine if response is valid and what type it is
    if (rxPkt.getTFlag() == PKT_TRNSMT_INCOMP) {
        //std::cout << "Received incomplete packet\n";
        return "Received incomplete response";
    }
    if (rxPkt.getPKType() == PKType) {
        return std::string(rxPkt.getData(), rxPkt.getPloadLength());
    }

    return "Unexpected response type";
}

/**
 * @brief Downloads the flight manual from the server and writes it to disk.
 *
 * Sends a @c req_file request, then receives a multi-packet file transfer
 * managed by a FileReceiver.  Progress is reported to stdout as a
 * @c "\\r" overwritten line showing kilobytes received, which the Python UI
 * reads and can display.
 *
 * @par Transfer protocol
 * 1. A single @c req_file request packet is sent.
 * 2. The first response packet is inspected:
 *    - @c pkt_empty → the server returned an error string; return it directly.
 *    - @c pkt_dat   → first chunk of the file; begin FileReceiver session.
 *    - Anything else → return @c "Unexpected packet type during manual transfer".
 * 3. Subsequent @c pkt_dat packets are fed to FileReceiver::processPacket()
 *    until isComplete() returns true.
 * 4. FileReceiver::finalize() flushes and closes the output file.
 *
 * @par Socket drain on FileReceiver failure
 * If FileReceiver::start() fails (e.g. cannot open @p outputPath), incoming
 * file packets are drained until a @c PKT_TRNSMT_COMP or @c pkt_empty packet
 * is received, keeping the socket in a consistent state before returning the
 * error.
 *
 * Error handling (each condition returns a descriptive string):
 * - Send failure            → @c "Failed to request flight manual"
 * - Receive failure         → @c "Transfer interrupted while receiving flight manual"
 * - CRC mismatch            → @c "CRC Checksum Failed during manual transfer"
 * - FileReceiver error      → error string from FileReceiver
 * - Unexpected packet type  → @c "Unexpected packet type during manual transfer"
 *
 * @param outputPath Filesystem path where the downloaded PDF should be saved
 *                   (e.g. @c "../client/flightmanual.pdf").
 * @return @c "Flight Manual downloaded to <outputPath>" on success, or an
 *         error description string on failure.
 */
std::string ClientEngine::downloadFlightManual(const std::string& outputPath) {
    // Build and send the file request
    Request txReq(req_file, 0, (char*)"");

    int reqSerialSize = 0;
    char* serializedReq = txReq.serializeRequest(&reqSerialSize);

    packet txPkt;
    txPkt.PopulPacket(serializedReq, reqSerialSize, (char)clientID, pkt_req);
    delete[] serializedReq;

    Logger::logPacket(txPkt, Logger::Direction::TX, "Flight manual download request");
    if (!PacketTransport::sendPacket((int)sock, txPkt)) {
        return "Failed to request flight manual";
    }

    // Receive and validate the first response packet
    packet firstPkt;
    if (!PacketTransport::receivePacket((int)sock, firstPkt)) {
        return "Transfer interrupted while receiving flight manual";
    }

    Logger::logPacket(firstPkt, Logger::Direction::RX, "Flight manual first packet");

    if (firstPkt.calcCRC() != firstPkt.GetCRC()) {
        return "CRC Checksum Failed during manual transfer";
    }
    // Server signalled an error via an empty packet — surface its payload
    if (firstPkt.getPKType() == pkt_empty) {
        return std::string(firstPkt.getData(), firstPkt.getPloadLength());
    }

    if (firstPkt.getPKType() != pkt_dat) {
        return "Unexpected packet type during manual transfer";
    }

    // Open the output file via FileReceiver
    FileReceiver receiver;
    std::string err;
    if (!receiver.start(outputPath, err)) {
        // Drain remaining file packets to keep the socket consistent
        packet drainPkt;

        while (true) {
            if (!PacketTransport::receivePacket((int)sock, drainPkt)) {
                break; // connection dropped, nothing more to do
            }

            // Stop draining once we hit the end of the file transfer
            if (drainPkt.getTFlag() == PKT_TRNSMT_COMP || drainPkt.getPKType() == pkt_empty) {
                break;
            }
        }

        return err;  // Now safe to return
    }

    // Process the first data chunk
    std::cout << "\rDownloading Flight Manual: 0 KB received..." << std::flush;

    if (!receiver.processPacket(firstPkt, err)) {
        std::cout << "\n";
        return err;
    }

    std::cout << "\rDownloading Flight Manual: "
              << (receiver.getBytesReceived() / 1024) << " KB received..." << std::flush;

              
    // Single-packet transfer edge case
    if (receiver.isComplete()) {
        std::cout << "\n";
        if (!receiver.finalize(err)) {
            return err;
        }
        return "Flight Manual downloaded to " + outputPath;
    }

    int pktcount = 1; // For logging purposes, counts the first packet as chunk 1
    int logInterval = 100; // Log every 100 packets to avoid excessive logging for large files

    // Receive remaining chunks until the transfer is complete
    while (true) {
        pktcount++;

        packet rxPkt;
        if (!PacketTransport::receivePacket((int)sock, rxPkt)) {
            std::cout << "\n";
            return "Transfer interrupted while receiving flight manual";
        }

        if (pktcount == logInterval) { // Log progress every logInterval packets
            Logger::logPacket(rxPkt, Logger::Direction::RX,
             "Flight manual chunk, " + std::to_string(receiver.getBytesReceived()) + " bytes received so far");
        }

        if (rxPkt.calcCRC() != rxPkt.GetCRC()) {
            std::cout << "\n";
            return "CRC Checksum Failed during manual transfer";
        }

        if (!receiver.processPacket(rxPkt, err)) {
            std::cout << "\n";
            return err;
        }

        if (pktcount == logInterval) { // Print progress every logInterval packets
            std::cout << "\rDownloading Flight Manual: "
                  << (receiver.getBytesReceived() / 1024) << " KB received..." << std::flush;
        }

        if (receiver.isComplete()) {
            break;
        }

        if (pktcount == logInterval) { pktcount = 0; } // Reset packet count every time the log interval is reached
        
    }

    std::cout << "\n";
    if (!receiver.finalize(err)) {
        return err;
    }

    return "Flight Manual downloaded to " + outputPath;
}