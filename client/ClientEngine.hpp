#pragma once
#include <winsock2.h>
#include <string>
#include "../shared/Packet.h"

class ClientEngine {
public:
    ClientEngine();
    ~ClientEngine();

    bool connectToServer(const std::string& ip, unsigned short port);
    void disconnect();

    std::string dataRequest(const std::string& data, pkTyFl pkType, reqtyp type);

private:
    SOCKET sock;
    unsigned int clientID;
};