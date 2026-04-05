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

    // --- Shared backend API (used by CLI and Python) ---
    std::string apiRequest(const std::string& cmd, const std::string& arg1, const std::string& arg2);
    std::string handleEmergency();
    std::string handleLogin(const std::string& user, const std::string& pass);
    std::string handleWeather(const std::string& location);
    std::string handleFlight(const std::string& flightId);
    std::string handleTaxi();
    std::string handleLand();
    std::string handleTelemetry(const std::string& data = "");
    std::string handleTraffic();
    std::string handleManualDownload();
    std::string handleResolveEmergency();


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
    std::string currentState;  // REQ-CLT-040: last known server state
};
