#include "ClientApp.hpp"
#include <iostream>
#include <cstdlib>

// ---------------------------------------------------------
// Constructor: performs WSAStartup + connectToServer
// ---------------------------------------------------------
ClientApp::ClientApp(const std::string& ip, unsigned short port)
    : ip(ip), port(port), initialized(false), connected(false)
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
            std::string result = client.pkRequest(input, pkt_emgcy);
            std::cout << "EMERGENCY_RESPONSE: " << result << "\n";
            std::cout.flush();
            continue;
        }

        // AUTH
        if (cmd == "Login") {
            std::string Username;
            std::cout << "Please enter Username: ";
            std::cin >> Username;

            std::string Password;
            std::cout << "Please enter Password: ";
            std::cin >> Password;

            std::string info = Username + "," + Password;
            std::string result = client.pkRequest(info, pkt_auth);

            std::cout << "LoginAuth: " << result << "\n";
            std::cout.flush();
            continue;
        }

        // PRE-FLIGHT
        if (cmd == "WEATHER") {
            std::cout << "Please enter location name: ";
            std::cin >> input;

            std::string result = client.dataRequest(input, req_weather);
            std::cout << "WEATHER_RESPONSE: " << result << "\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "FLIGHT") {
            std::cout << "Please enter Flight ID: ";
            std::cin >> input;

            std::string result = client.dataRequest(input, req_fplan);
            std::cout << "Flight Plan: " << result << "\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TAXI") {
            std::string result = client.dataRequest(input, req_taxi);
            std::cout << "Taxi Response: " << result << "\n";
            std::cout.flush();
            continue;
        }

        // ACTIVE AIRSPACE
        if (cmd == "LAND") {
            std::string result = client.dataRequest(input, req_taxi);
            std::cout << "Clearance_Request: " << result << "\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TELEM") {
            std::string result = client.dataRequest(input, req_telemetry);
            std::cout << "Telemetry Response: " << result << "\n";
            std::cout.flush();
            continue;
        }

        if (cmd == "TRAFFIC") {
            std::string result = client.dataRequest(input, req_traffic);
            std::cout << "Air Traffic Response: " << result << "\n";
            std::cout.flush();
            continue;
        }

        // DATA TRANSFER
        if (cmd == "MANUAL") {
            std::string result = client.downloadFlightManual("../client/flightmanual.pdf");
            std::cout << "Manual Response: " << result << "\n";
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
