#include "ClientApp.hpp"
#include <iostream>
#include <cstdlib>

// ---------------------------------------------------------
// Constructor: performs WSAStartup + connectToServer
// ---------------------------------------------------------
ClientApp::ClientApp(const std::string& ip, unsigned short port)
    : ip(ip), port(port), initialized(false), connected(false), currentState("STARTUP/AUTH")
{
    // --- WSA Startup (same as original main.cpp) ---
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cout << "WSAStartup failed\n";
        return;
    }
    initialized = true;

    // --- Connect to server ---
    if (!client.connectToServer(ip, port)) {
        std::cout << "CONNECT_FAILED\n";
        return;
    }

    connected = true;
    std::cout << "CONNECTED\n";
    std::cout.flush();
}

// ---------------------------------------------------------
// Destructor: ensures cleanup
// ---------------------------------------------------------
ClientApp::~ClientApp() {
    shutdown();
}

// ---------------------------------------------------------
// This is your original command loop, unchanged
// ---------------------------------------------------------
void ClientApp::runCommandLoop() {
    if (!initialized || !connected) return;

    std::string cmd;
    while (std::cin >> cmd) {
        std::string input = "";

        // QUIT
        if (cmd == "QUIT") {
            std::cout << "BYE\n";
            std::cout.flush();
            break;
        }

        // EMERGENCY
        if (cmd == "EMERGENCY") {
            std::cout << "EMERGENCY_RESPONSE: " << handleEmergency() << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        // AUTH
        if (cmd == "Login") {
            std::string Username, Password;
            std::cout << "Please enter Username: ";
            std::cin >> Username;
            std::cout << "Please enter Password: ";
            std::cin >> Password;

            std::cout << "LoginAuth: " << handleLogin(Username, Password) << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        // PRE-FLIGHT
        if (cmd == "WEATHER") {
            std::cout << "Please enter location name: ";
            std::cin >> input;

            std::cout << "WEATHER_RESPONSE: " << handleWeather(input) << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "FLIGHT") {
            std::cout << "Please enter Flight ID: ";
            std::cin >> input;

            std::cout << "Flight Plan: " << handleFlight(input) << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TAXI") {
            std::cout << "Taxi Response: " << handleTaxi() << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TELEM") {
            std::cout << "Telemetry Response: " << handleTelemetry() << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TRAFFIC") {
            std::cout << "Air Traffic Response: " << handleTraffic() << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        // DATA TRANSFER
        if (cmd == "MANUAL") {
            std::cout << "Manual Response: " << handleManualDownload() << "\n";
            std::cout << "[Tower State: " << currentState << "]\n";
            std::cout.flush();
            continue;
        }

        // Unknown command
        std::string rest;
        std::getline(std::cin, rest);
        std::cout << "ERROR Unknown command\n";
        std::cout.flush();
    }
}

// ----------------------
// Backend API functions
// ----------------------
std::string ClientApp::apiRequest(const std::string& cmd, const std::string& arg1, const std::string& arg2) {
    if (!initialized || !connected)
        return "ERROR_NOT_CONNECTED";

    if (cmd == "EMERGENCY") return handleEmergency();
    if (cmd == "LOGIN")     return handleLogin(arg1, arg2);
    if (cmd == "WEATHER")   return handleWeather(arg1);
    if (cmd == "FLIGHT")    return handleFlight(arg1);
    if (cmd == "TAXI")      return handleTaxi();
    if (cmd == "LAND")      return handleLand();
    if (cmd == "TELEM")     return handleTelemetry();
    if (cmd == "TRAFFIC")   return handleTraffic();
    if (cmd == "MANUAL")    return handleManualDownload();

    return "ERROR_UNKNOWN_CMD";
}

std::string ClientApp::handleEmergency() {
    std::string resp = client.pkRequest("", pkt_emgcy);
    // Response contains "State changed to: X" — parse and track it (REQ-CLT-040).
    auto pos = resp.find("State changed to: ");
    if (pos != std::string::npos)
        currentState = resp.substr(pos + 18);
    return resp;
}

std::string ClientApp::handleLogin(const std::string& user, const std::string& pass) {
    std::string resp = client.pkRequest(user + "," + pass, pkt_auth);
    if (resp.find("Authentication Successful") != std::string::npos)
        currentState = "PRE_FLIGHT";
    return resp;
}

std::string ClientApp::handleWeather(const std::string& location) {
    return client.dataRequest(location, req_weather);
}

std::string ClientApp::handleFlight(const std::string& flightId) {
    return client.dataRequest(flightId, req_fplan);
}

std::string ClientApp::handleTaxi() {
    std::string resp = client.dataRequest("", req_taxi);
    if (resp.find("departure") != std::string::npos)
        currentState = "ACTIVE_AIRSPACE";
    else if (resp.find("arrival") != std::string::npos)
        currentState = "PRE_FLIGHT";
    return resp;
}

std::string ClientApp::handleLand() {
    return handleTaxi();
}

std::string ClientApp::handleTelemetry() {
    return client.dataRequest("", req_telemetry);
}

std::string ClientApp::handleTraffic() {
    return client.dataRequest("", req_traffic);
}

std::string ClientApp::handleManualDownload() {
    return client.downloadFlightManual("../client/flightmanual.pdf");
}


// ---------------------------------------------------------
// Cleanup function
// ---------------------------------------------------------
void ClientApp::shutdown() {
    if (connected) {
        client.disconnect();
        connected = false;
    }
    if (initialized) {
        WSACleanup();
        initialized = false;
    }
}
