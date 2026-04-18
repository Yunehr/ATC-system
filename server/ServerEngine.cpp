/**
 * @file ServerEngine.cpp
 * @brief Implementation of the ServerEngine class.
 *
 * Manages the TCP listen socket, accepts incoming client connections
 * (one at a time, sequentially), and drives the per-client packet
 * dispatch loop.  Each client session owns its own StateMachine instance,
 * so state is fully isolated between connections.
 *
 * @par Structured stdout protocol
 * In addition to human-readable log lines, ServerEngine writes machine-
 * readable prefixed lines consumed by the Python ATCScreen dashboard:
 *
 * | Prefix                          | Meaning                                  |
 * |---------------------------------|------------------------------------------|
 * | @c "STATE: <name>"              | Server state changed; update UI indicator.|
 * | @c "TRANSFER START: total=<N>"  | File transfer beginning; N total bytes.  |
 * | @c "TRANSFER PROGRESS: current=<C> total=<T>" | Chunk sent.             |
 * | @c "TRANSFER COMPLETE: current=<C> total=<T>" | Transfer finished.      |
 * | @c "Client connected!"          | New TCP connection accepted.             |
 * | @c "Client disconnected."       | TCP connection closed.                   |
 *
 * @par Packet dispatch summary (handleClient)
 * | Packet type  | Handler                                                   |
 * |--------------|-----------------------------------------------------------|
 * | @c pkt_auth  | ClientSession::authenticate(); StateMachine::onAuthSuccess()|
 * | @c pkt_emgcy | StateMachine::onEmergency(); sends acknowledgement.       |
 * | @c pkt_req   | Decoded to @c reqtyp and dispatched to service handlers.  |
 * | Other        | Sends @c pkt_empty error response.                        |
 */
 
#include "ServerEngine.hpp"
#include "../shared/PacketTransport.hpp"
#include "../shared/packet.h"
#include "../shared/Request.h"
#include "WeatherService.hpp"
#include "FileTransferManager.hpp"
#include "ClientSession.hpp"
#include "StateMachine.hpp"
#include "Logger.h"
#include <iostream>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Opens the flight manual PDF by probing four candidate paths.
 *
 * Tries paths in priority order:
 * -# @c ../data/FlightManual.pdf
 * -# @c data/FlightManual.pdf
 * -# @c ../data/flightmanual.pdf  (lowercase fallback)
 * -# @c data/flightmanual.pdf     (lowercase fallback)
 *
 * @return An @c std::ifstream open in binary mode on the first path found,
 *         or in a failed state if none of the paths exist.
 */
static std::ifstream openManualFile() {
    std::ifstream in("../data/FlightManual.pdf", std::ios::binary);
    if (in.is_open()) return in;

    in.open("data/FlightManual.pdf", std::ios::binary);
    if (in.is_open()) return in;

    in.open("../data/flightmanual.pdf", std::ios::binary);
    if (in.is_open()) return in;

    in.open("data/flightmanual.pdf", std::ios::binary);
    return in;
}

/**
 * @brief Returns the size of the flight manual PDF in bytes.
 *
 * Opens the file via openManualFile(), seeks to the end, and reports the
 * position.
 *
 * @return File size in bytes, or @c 0 if the file could not be opened.
 */
static std::streamsize getManualFileSize() {
    std::ifstream in = openManualFile();
    if (!in.is_open()) {
        return 0;
    }

    in.seekg(0, std::ios::end);
    return in.tellg();
}

// ---------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------
 
/**
 * @brief Constructs a ServerEngine with an invalid listen socket and stopped state.
 */
ServerEngine::ServerEngine() : listenSock(INVALID_SOCKET), running(false) {}

/**
 * @brief Destructor — calls stop() to close the listen socket.
 */
ServerEngine::~ServerEngine() {
    stop();
}

// ---------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------
 
/**
 * @brief Binds a TCP listen socket to the given port and begins listening.
 *
 * Creates an @c AF_INET/@c SOCK_STREAM socket, binds it to @c INADDR_ANY
 * on @p port, and calls listen().  Sets @ref running to @c true and emits
 * @c "STATE: STARTUP/AUTH" to stdout on success.
 *
 * On any failure the socket is closed and reset to @c INVALID_SOCKET before
 * returning @c false.
 *
 * @param port TCP port to listen on (e.g. @c 8080).
 * @return @c true  if the socket is bound and listening successfully.
 * @return @c false if socket(), bind(), or listen() failed.
 */
bool ServerEngine::start(unsigned short port) {
    listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cout << "socket() failed\n";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "bind() failed\n";
        closesocket(listenSock);
        listenSock = INVALID_SOCKET;
        return false;
    }

    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        std::cout << "listen() failed\n";
        closesocket(listenSock);
        listenSock = INVALID_SOCKET;
        return false;
    }

    running = true;
    std::cout << "Server listening on port " << port << "\n";
    std::cout << "STATE: STARTUP/AUTH\n";
    std::cout.flush();
    return true;
}

/**
 * @brief Blocks in the accept loop, handling one client at a time.
 *
 * Calls accept() in a loop.  For each accepted connection, calls
 * handleClient() (which blocks until the client disconnects), then closes
 * the client socket before accepting the next connection.
 *
 * Invalid accept() results are logged and skipped rather than aborting the loop.
 * The loop exits when @ref running is set to @c false by stop().
 */
void ServerEngine::run() {
    while (running) {
        sockaddr_in clientAddr{};
        int len = sizeof(clientAddr);

        SOCKET clientSock = accept(listenSock, (sockaddr*)&clientAddr, &len);
        if (clientSock == INVALID_SOCKET) {
            if (running) {
                std::cout << "accept() failed\n";
            }
            continue;
        }

        std::cout << "Client connected!\n";
        std::cout.flush();

        handleClient(clientSock);

        closesocket(clientSock);
        std::cout << "Client disconnected.\n";
    }
}

/**
 * @brief Sets @ref running to @c false and closes the listen socket.
 *
 * Safe to call multiple times — the @c INVALID_SOCKET guard prevents
 * double-close.
 */
void ServerEngine::stop() {
    running = false;

    if (listenSock != INVALID_SOCKET) {
        closesocket(listenSock);
        listenSock = INVALID_SOCKET;
    }
}

// ---------------------------------------------------------
// File transfer
// ---------------------------------------------------------
 
/**
 * @brief Streams the flight manual PDF to the client in fixed-size chunks.
 *
 * @par Chunk payload layout
 * Each @c pkt_dat packet payload contains:
 * @code
 * [ 4 bytes: uint32_t write offset ] [ N bytes: file data ]
 * @endcode
 * This matches the layout expected by FileReceiver::processPacket() on the
 * client side.
 *
 * @par Transmission flags
 * - All chunks except the last carry @c PKT_TRNSMT_INCOMP.
 * - The final chunk (detected via @c src.peek() == EOF) carries @c PKT_TRNSMT_COMP.
 *
 * @par Progress reporting
 * Emits @c "TRANSFER START:", @c "TRANSFER PROGRESS:", and
 * @c "TRANSFER COMPLETE:" lines to stdout for the ATCScreen dashboard.
 *
 * @par Edge case — empty file
 * If the file opens successfully but contains zero bytes, a single zero-length
 * @c pkt_dat packet with @c PKT_TRNSMT_COMP is sent so the client can exit
 * its receive loop cleanly.
 *
 * @param clientSock The connected client socket to write chunks to.
 * @param clientId   Client ID stamped into every outgoing packet header.
 * @return @c true  if all chunks were sent successfully.
 * @return @c false if the file could not be opened or any send failed.
 */
bool ServerEngine::sendFlightManual(SOCKET clientSock, unsigned char clientId) {
    std::ifstream src = openManualFile();
    if (!src.is_open()) {
        return false;
    }

    const std::streamsize totalBytes = getManualFileSize();
    std::cout << "TRANSFER START: total=" << (long long)totalBytes << std::endl;

    const int chunkSize = MAX_PKTSIZE - (int)sizeof(uint32_t);
    char fileBuffer[MAX_PKTSIZE] = { 0 };
    char payload[MAX_PKTSIZE] = { 0 };
    uint32_t currentOffset = 0;
    bool sentAnyChunk = false;

    int pktcount = 0; // For logging purposes, counts how many chunks have been sent since the last log
    int logInterval = 1000; // Log every 1000 chunks to avoid excessive logging for large files

    while (true) {

        src.read(fileBuffer, chunkSize);
        std::streamsize bytesRead = src.gcount();
        if (bytesRead <= 0) {
            break;
        }

        std::memcpy(payload, &currentOffset, sizeof(uint32_t));
        std::memcpy(payload + sizeof(uint32_t), fileBuffer, (size_t)bytesRead);

        packet chunk;
        chunk.PopulPacket(payload, (int)bytesRead + (int)sizeof(uint32_t), (char)clientId, pkt_dat);

        bool isLastChunk = (src.peek() == EOF);
        chunk.setTransmitFlag(isLastChunk ? PKT_TRNSMT_COMP : PKT_TRNSMT_INCOMP);

        if (!PacketTransport::sendPacket((int)clientSock, chunk)) {
            return false;
        }

        pktcount++;
        if (pktcount == logInterval) {
            std::cout << "TRANSFER PROGRESS: current=" << (long long)(currentOffset + (uint32_t)bytesRead)
                  << " total=" << (long long)totalBytes << std::endl;
            pktcount = 0; // Reset packet count every time the log interval is reached
        }
        sentAnyChunk = true;
        currentOffset += (uint32_t)bytesRead;
    }
    // Edge case: file existed but was empty — send a completion signal anyway.
    if (!sentAnyChunk) {
        packet eofChunk;
        eofChunk.PopulPacket(payload, (int)sizeof(uint32_t), (char)clientId, pkt_dat);
        eofChunk.setTransmitFlag(PKT_TRNSMT_COMP);
        if (!PacketTransport::sendPacket((int)clientSock, eofChunk)) {
            return false;
        }

        std::cout << "TRANSFER COMPLETE: current=0 total=0" << std::endl;
        return true;
    }

    std::cout << "TRANSFER COMPLETE: current=" << (long long)currentOffset
              << " total=" << (long long)totalBytes << std::endl;
    return true;
}

// ---------------------------------------------------------
// Per-client dispatch loop
// ---------------------------------------------------------
 
/**
 * @brief Receives and dispatches packets for a single connected client.
 *
 * Runs a blocking receive loop until the client disconnects or a send error
 * occurs.  Each iteration:
 * -# Receives one packet via PacketTransport::receivePacket().
 * -# Builds a human-readable description and logs the packet (REQ-LOG-010/030/040).
 * -# Verifies the CRC; sends a @c pkt_empty error and continues on mismatch.
 * -# Dispatches to the appropriate handler based on packet type:
 *
 * @par pkt_req dispatch
 * The first byte of the payload is cast to @c reqtyp and checked against
 * StateMachine::canHandle().  Denied requests receive a @c pkt_empty
 * (@c req_file) or @c pkt_dat (all others) error response.  Permitted
 * requests are forwarded to the relevant service:
 *
 * | reqtyp          | Service called                                |
 * |-----------------|-----------------------------------------------|
 * | @c req_weather  | WeatherService::getWeather()                  |
 * | @c req_telemetry| FileTransferManager::logTelemetry()           |
 * | @c req_file     | sendFlightManual() (multi-packet, no @c continue)|
 * | @c req_taxi     | FileTransferManager::getTaxiClearance()       |
 * | @c req_fplan    | FileTransferManager::getFlightPlan()          |
 * | @c req_traffic  | FileTransferManager::getTraffic()             |
 * | @c req_resolve  | StateMachine::onEmergencyResolved()           |
 *
 * @par pkt_auth
 * Credentials are forwarded to ClientSession::authenticate().  On success,
 * StateMachine::onAuthSuccess() is called and @p pilotId is extracted from
 * the credential string for use in subsequent telemetry log entries.
 *
 * @par pkt_emgcy
 * StateMachine::onEmergency() is called and an acknowledgement packet is sent.
 *
 * @note Debug packet-field dumps (CRC, TYPE, FLAG, etc.) are written to
 *       stdout and should be removed before production release.
 *
 * @param clientSock The accepted client socket for this session.
 */
void ServerEngine::handleClient(SOCKET clientSock) {
    StateMachine stateMachine;
    std::string pilotId = "UNKNOWN";

    while (true) {
        packet rxPkt;

        if (!PacketTransport::receivePacket((int)clientSock, rxPkt)) {
            break;
        }

        unsigned char type = rxPkt.getPKType();
        unsigned char clientID = rxPkt.getCLID();

        // REQ-LOG-010/030/040: log every received packet with a human-readable description.
        {
            std::string rxDesc;
            if (type == pkt_auth) {
                rxDesc = "Auth attempt from client " + std::to_string((int)clientID);
            } else if (type == pkt_emgcy) {
                rxDesc = "Emergency declared by client " + std::to_string((int)clientID);
            } else if (type == pkt_req && rxPkt.getPloadLength() > 0) {
                int reqByte = (unsigned char)rxPkt.getData()[0];
                switch (reqByte) {
                case req_weather:
                    rxDesc = "Weather request";
                    break;
                case req_telemetry: {
                    // REQ-LOG-040: record Client Status and Last Location from telemetry body.
                    int bodyLen = (int)rxPkt.getPloadLength() - 1;
                    std::string body = (bodyLen > 0)
                        ? std::string(rxPkt.getData() + 1, bodyLen)
                        : "NO_TELEMETRY";
                    rxDesc = "Telemetry update - Client Status and Last Location: " + body;
                    break;
                }
                case req_file:    rxDesc = "Flight manual download request"; break;
                case req_taxi:    rxDesc = "Taxi clearance request";         break;
                case req_fplan:   rxDesc = "Flight plan request";            break;
                case req_traffic: rxDesc = "Traffic request";                break;
                default:          rxDesc = "Unknown request type " + std::to_string(reqByte);
                }
            } else {
                rxDesc = "Unknown packet type " + std::to_string((int)type);
            }
            Logger::logPacket(rxPkt, Logger::Direction::RX, stateMachine.getStateName(), rxDesc);
        }

        // CRC check — send error and continue (don't disconnect) on mismatch.
        if (rxPkt.calcCRC() != rxPkt.GetCRC()){
            std::string msg = "CRC Checksum Failed";

            packet err;
            err.PopulPacket((char*)msg.c_str(), (int)msg.size(), clientID, pkt_empty);

            Logger::logPacket(err, Logger::Direction::TX, stateMachine.getStateName(), msg);
            if (!PacketTransport::sendPacket((int)clientSock, err)) {
                break;
            }
        }

        // -----------------------------------------------------------------
        // pkt_req dispatch
        // -----------------------------------------------------------------
        if (type == pkt_req) {
            Request rxReq(rxPkt.getData(), rxPkt.getPloadLength());
            std::string data;
            reqtyp reqType = static_cast<reqtyp>(rxReq.getType());

            if (!stateMachine.canHandle(reqType)) {
                data = "Request denied in state: " + stateMachine.getStateName();

                packet denyResp;
                unsigned char denyType = (reqType == req_file) ? pkt_empty : pkt_dat;
                denyResp.PopulPacket((char*)data.c_str(), (int)data.size(), clientID, denyType);

                Logger::logPacket(denyResp, Logger::Direction::TX, stateMachine.getStateName(), data);
                if (!PacketTransport::sendPacket((int)clientSock, denyResp)) {
                    break;
                }
                continue;
            }
            else if (reqType == req_weather) {
                std::string location(rxReq.getBody(), rxReq.getBsize());

                std::cout << "REQUEST RECEIVED: " << location << "\n";
                std::cout << "Client ID: " << (int)clientID << "\n";

                data = WeatherService::getWeather(location);
                stateMachine.onRequestHandled(reqType);
            }
            else if (reqType == req_telemetry) {
                std::string telemetry(rxReq.getBody(), rxReq.getBsize());
                data = FileTransferManager::logTelemetry(telemetry, clientID, pilotId);
                stateMachine.onRequestHandled(reqType);
            }
            else if (reqType == req_file) {
                if (!sendFlightManual(clientSock, clientID)) {
                    data = "Flight Manual: transfer failed or file missing";
                    packet resp;
                    resp.PopulPacket((char*)data.c_str(), (int)data.size(), clientID, pkt_empty);
                    Logger::logPacket(resp, Logger::Direction::TX, stateMachine.getStateName(), data);
                    if (!PacketTransport::sendPacket((int)clientSock, resp)) {
                        break;
                    }
                } else {
                    stateMachine.onRequestHandled(reqType);
                    stateMachine.onDataTransferComplete();
                    std::cout << "STATE: " << stateMachine.getStateName() << "\n";
                    std::cout.flush();
                    std::string xferMsg = "Flight Manual transfer complete";
                    packet xferLog;
                    xferLog.PopulPacket((char*)xferMsg.c_str(), (int)xferMsg.size(), clientID, pkt_dat);
                    Logger::logPacket(xferLog, Logger::Direction::TX, stateMachine.getStateName(), xferMsg);
                }
                continue;
            }

            else if (reqType == req_taxi) {
                data = FileTransferManager::getTaxiClearance(stateMachine.getStateName());
                stateMachine.onRequestHandled(reqType);
            }
            else if (reqType == req_fplan) {
                std::string flightId(rxReq.getBody(), rxReq.getBsize());
                data = FileTransferManager::getFlightPlan(flightId);
                stateMachine.onRequestHandled(reqType);
            }
            else if (reqType == req_traffic) {
                std::string flightId(rxReq.getBody(), rxReq.getBsize());
                data = FileTransferManager::getTraffic(flightId);
                stateMachine.onRequestHandled(reqType);
            }
            else if (reqType == req_resolve) {
                stateMachine.onEmergencyResolved();
                data = "Emergency resolved. State changed to: " + stateMachine.getStateName();
                std::cout << "EMERGENCY RESOLVED by client " << (int)clientID
                          << ". Current state: " << stateMachine.getStateName() << "\n";
                std::cout.flush();
            }
            else {
                data = "Unknown request type";
            }

            std::cout << "STATE: " << stateMachine.getStateName() << "\n";

            packet resp;
            resp.PopulPacket((char*)data.c_str(), (int)data.size(), clientID, pkt_dat);

            std::cout << "DATA: ";
            std::cout.write(resp.getData(), resp.getPloadLength());
            std::cout << std::endl;

            // just for me to debug
            std::cout << "CRC: " << resp.calcCRC() << std::endl;
            std::cout << "TYPE: " << (int)resp.getPKType() << std::endl;
            std::cout << "FLAG: " << (int)resp.getTFlag() << std::endl;
            std::cout << "CLIENT ID: " << (int)resp.getCLID() << std::endl;
            std::cout << "LENGTH: " << (int)resp.getPloadLength() << std::endl;

            Logger::logPacket(resp, Logger::Direction::TX, stateMachine.getStateName(), data);
            if (!PacketTransport::sendPacket((int)clientSock, resp)) {
                break;
            }
            continue;
        }

        // -----------------------------------------------------------------
        // pkt_auth dispatch
        // -----------------------------------------------------------------
        if (type == pkt_auth) {
            std::string credentials(rxPkt.getData(), rxPkt.getPloadLength());
            std::cout << "AUTH ATTEMPT: " << credentials << "\n";
            std::cout << "Client ID: " << (int)clientID << "\n";

            std::string response = ClientSession::authenticate(credentials);
            if (response.find("Authentication Successful") != std::string::npos) {
                stateMachine.onAuthSuccess();
                auto commaPos = credentials.find(',');
                if (commaPos != std::string::npos)
                    pilotId = credentials.substr(0, commaPos);
                std::cout << "STATE: " << stateMachine.getStateName() << "\n";
                std::cout.flush();
            }

            packet authResp;
            authResp.PopulPacket((char*)response.c_str(), (int)response.size(), clientID, pkt_auth);

            Logger::logPacket(authResp, Logger::Direction::TX, stateMachine.getStateName(), response);
            if (!PacketTransport::sendPacket((int)clientSock, authResp)) {
                break;
            }
            continue;
        }

        // -----------------------------------------------------------------
        // pkt_emgcy dispatch
        // -----------------------------------------------------------------
        if (type == pkt_emgcy) {
            stateMachine.onEmergency();
            std::cout << "STATE: " << stateMachine.getStateName() << "\n";
            std::cout.flush();

            const std::string response = "Emergency acknowledged. State changed to: " + stateMachine.getStateName();
            packet emgResp;
            emgResp.PopulPacket((char*)response.c_str(), (int)response.size(), clientID, pkt_emgcy);

            Logger::logPacket(emgResp, Logger::Direction::TX, stateMachine.getStateName(), response);
            if (!PacketTransport::sendPacket((int)clientSock, emgResp)) {
                break;
            }

            std::cout << "EMERGENCY TRIGGERED by client " << (int)clientID
                      << ". Current state: " << stateMachine.getStateName() << "\n";
            std::cout.flush();
            continue;
        }
        
        // -----------------------------------------------------------------
        // Unknown packet type fallthrough
        // -----------------------------------------------------------------
        {
            std::string msg = "Unknown packet type";

            packet err;
            err.PopulPacket((char*)msg.c_str(), (int)msg.size(), clientID, pkt_empty);

            Logger::logPacket(err, Logger::Direction::TX, stateMachine.getStateName(), msg);
            if (!PacketTransport::sendPacket((int)clientSock, err)) {
                break;
            }
        }
    }
}