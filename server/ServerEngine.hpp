#pragma once
#include <winsock2.h>

class ServerEngine {
public:
    ServerEngine();
    ~ServerEngine();

    bool start(unsigned short port);
    void run();               // accept loop
    void stop();
    //moved for testing purposes
    bool sendFlightManual(SOCKET clientSock, unsigned char clientId);
private:
    SOCKET listenSock;
    bool running;

    void handleClient(SOCKET clientSock);
    
};