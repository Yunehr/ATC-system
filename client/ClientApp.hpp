#pragma once
#include "ClientEngine.hpp"
#include <winsock2.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

class ClientApp {
public:
    // Constructor: performs WSAStartup + connectToServer
    ClientApp(const std::string& ip = "127.0.0.1", unsigned short port = 54000);

    // Destructor: performs cleanup
    ~ClientApp();

    // This contains your original while(std::cin >> cmd) loop
    // Later this will be replaced by Python-driven commands
    void runCommandLoop();

    // Explicit cleanup (also called by destructor)
    void shutdown();

    // ---------------------------------------------------------
    // FUTURE PYBIND11 API:
    // - std::string sendCommand(const std::string& cmd)
    // - std::string login(username, password)
    // - std::string getWeather(location)
    // - etc.
    // ---------------------------------------------------------

private:
    std::string ip;
    unsigned short port;
    WSADATA wsa;
    ClientEngine client;

    bool initialized;
    bool connected;
};
