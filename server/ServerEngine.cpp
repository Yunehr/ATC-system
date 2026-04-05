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

static std::streamsize getManualFileSize() {
    std::ifstream in = openManualFile();
    if (!in.is_open()) {
        return 0;
    }

    in.seekg(0, std::ios::end);
    return in.tellg();
}

ServerEngine::ServerEngine() : listenSock(INVALID_SOCKET), running(false) {}

ServerEngine::~ServerEngine() {
    stop();
}

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
    return true;
}

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

        handleClient(clientSock);

        closesocket(clientSock);
        std::cout << "Client disconnected.\n";
    }
}

void ServerEngine::stop() {
    running = false;

    if (listenSock != INVALID_SOCKET) {
        closesocket(listenSock);
        listenSock = INVALID_SOCKET;
    }
}

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

        std::cout << "TRANSFER PROGRESS: current=" << (long long)(currentOffset + (uint32_t)bytesRead)
                  << " total=" << (long long)totalBytes << std::endl;

        sentAnyChunk = true;
        currentOffset += (uint32_t)bytesRead;
    }

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

void ServerEngine::handleClient(SOCKET clientSock) {
    StateMachine stateMachine;

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

        if (rxPkt.calcCRC() != rxPkt.GetCRC()){
            std::string msg = "CRC Checksum Failed";

            packet err;
            err.PopulPacket((char*)msg.c_str(), (int)msg.size(), clientID, pkt_empty);

            Logger::logPacket(err, Logger::Direction::TX, stateMachine.getStateName(), msg);
            if (!PacketTransport::sendPacket((int)clientSock, err)) {
                break;
            }
        }

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
                data = FileTransferManager::logTelemetry(telemetry, clientID);
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
            else {
                data = "Unknown request type";
            }

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

        if (type == pkt_auth) {
            std::string credentials(rxPkt.getData(), rxPkt.getPloadLength());
            std::cout << "AUTH ATTEMPT: " << credentials << "\n";
            std::cout << "Client ID: " << (int)clientID << "\n";

            std::string response = ClientSession::authenticate(credentials);
            if (response.find("Authentication Successful") != std::string::npos) {
                stateMachine.onAuthSuccess();
            }

            packet authResp;
            authResp.PopulPacket((char*)response.c_str(), (int)response.size(), clientID, pkt_auth);

            Logger::logPacket(authResp, Logger::Direction::TX, stateMachine.getStateName(), response);
            if (!PacketTransport::sendPacket((int)clientSock, authResp)) {
                break;
            }
            continue;
        }

        if (type == pkt_emgcy) {
            stateMachine.onEmergency();

            const std::string response = "Emergency acknowledged. State changed to: " + stateMachine.getStateName();
            packet emgResp;
            emgResp.PopulPacket((char*)response.c_str(), (int)response.size(), clientID, pkt_emgcy);

            Logger::logPacket(emgResp, Logger::Direction::TX, stateMachine.getStateName(), response);
            if (!PacketTransport::sendPacket((int)clientSock, emgResp)) {
                break;
            }

            std::cout << "EMERGENCY TRIGGERED by client " << (int)clientID
                      << ". Current state: " << stateMachine.getStateName() << "\n";
            continue;
        }

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