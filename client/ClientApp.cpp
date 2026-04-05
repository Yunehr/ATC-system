/**
 * @file ClientApp.cpp
 * @brief Implementation of the ClientApp class.
 *
 * Handles Winsock initialisation, server connection lifecycle, and all
 * aviation-domain request handlers (authentication, weather, flight plan,
 * taxi/landing, telemetry, air traffic, and flight manual download).
 *
 * Commands are dispatched through apiRequest(), which is called by the
 * stdin bridge in main.cpp.  State transitions are tracked in
 * @ref currentState and mirror the server-side state machine.
 */
#include "ClientApp.hpp"
#include <iostream>
#include <cstdlib>
#include <random>
#include <sstream>
#include <iomanip>

// ---------------------------------------------------------
// Constructor: performs WSAStartup + connectToServer
// ---------------------------------------------------------
/**
 * @brief Constructs a ClientApp, initialises Winsock, and connects to the server.
 *
 * Performs WSAStartup and then attempts to open a TCP connection to the
 * backend server.  On success, writes @c "CONNECTED\\n" to stdout so that
 * the parent process knows the bridge is ready.  On any failure the
 * corresponding flag (@ref initialized / @ref connected) is left false and
 * subsequent calls to apiRequest() will return @c "ERROR_NOT_CONNECTED".
 *
 * @param ip   IPv4 address of the backend server (e.g. @c "127.0.0.1").
 * @param port TCP port the backend server is listening on (e.g. @c 8080).
 */
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
 
/**
 * @brief Destructor — ensures the socket and Winsock are cleaned up.
 *
 * Delegates to shutdown().
 */
ClientApp::~ClientApp() {
    shutdown();
}

// ---------------------------------------------------------
// Interactive command loop (legacy / debug)
// ---------------------------------------------------------
 
/**
 * @brief Runs an interactive stdin command loop (legacy interface).
 *
 * Reads whitespace-delimited tokens from stdin and dispatches them to the
 * appropriate handler.  This loop is retained for debugging purposes; the
 * production code path uses apiRequest() instead.
 *
 * Recognised commands:
 * | Command    | Action                                      |
 * |------------|---------------------------------------------|
 * | QUIT       | Prints @c "BYE" and exits the loop.         |
 * | EMERGENCY  | Calls handleEmergency().                    |
 * | Login      | Prompts for username/password interactively.|
 * | WEATHER    | Prompts for a location name.                |
 * | FLIGHT     | Prompts for a flight ID.                    |
 * | TAXI       | Calls handleTaxi().                         |
 * | TELEM      | Calls handleTelemetry().                    |
 * | TRAFFIC    | Calls handleTraffic().                      |
 * | MANUAL     | Calls handleManualDownload().               |
 *
 * Does nothing if the app is not initialised and connected.
 */
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

 
// ---------------------------------------------------------
// Primary API dispatcher
// ---------------------------------------------------------
 
/**
 * @brief Parses and dispatches a command received from the stdin bridge.
 *
 * This is the main entry point called by main.cpp for every line read from
 * stdin.  It maps command tokens to handler methods and returns their
 * response strings, which are then written back to stdout by the caller.
 *
 * @param cmd  The command token (e.g. @c "LOGIN", @c "WEATHER", @c "TAXI").
 * @param arg1 First optional argument (e.g. username, airport code, flight ID).
 * @param arg2 Second optional argument (e.g. password).
 * @return A response string to be forwarded to the parent Python process, or
 *         @c "ERROR_NOT_CONNECTED" if the socket is not ready, or
 *         @c "ERROR_UNKNOWN_CMD" if @p cmd is not recognised.
 */
std::string ClientApp::apiRequest(const std::string& cmd, const std::string& arg1, const std::string& arg2) {
    if (!initialized || !connected)
        return "ERROR_NOT_CONNECTED";

    if (cmd == "EMERGENCY") return handleEmergency();
    if (cmd == "RESOLVE")   return handleResolveEmergency();
    if (cmd == "LOGIN")     return handleLogin(arg1, arg2);
    if (cmd == "WEATHER")   return handleWeather(arg1);
    if (cmd == "FLIGHT")    return handleFlight(arg1);
    if (cmd == "TAXI")      return handleTaxi();
    if (cmd == "LAND")      return handleLand();
    if (cmd == "TELEM")     return handleTelemetry(arg1);
    if (cmd == "TRAFFIC")   return handleTraffic();
    if (cmd == "MANUAL")    return handleManualDownload();

    return "ERROR_UNKNOWN_CMD";
}

// ---------------------------------------------------------
// Handler implementations
// ---------------------------------------------------------
 
/**
 * @brief Sends an emergency-resolve request to the server.
 *
 * On a successful response (containing @c "Emergency resolved"), parses the
 * new state from the @c "State changed to: " substring and updates
 * @ref currentState accordingly.
 *
 * @return The raw response string from the server.
 */
std::string ClientApp::handleResolveEmergency() {
    std::string resp = client.dataRequest("", req_resolve);
    if (resp.find("Emergency resolved") != std::string::npos) {
        auto pos = resp.find("State changed to: ");
        if (pos != std::string::npos)
            currentState = resp.substr(pos + 18);
    }
    return resp;
}

/**
 * @brief Sends an emergency declaration packet to the server.
 *
 * Parses the @c "State changed to: " substring in the server response and
 * updates @ref currentState (REQ-CLT-040).
 *
 * @return The raw response string from the server.
 */
std::string ClientApp::handleEmergency() {
    std::string resp = client.pkRequest("", pkt_emgcy);
    // Response contains "State changed to: X" — parse and track it (REQ-CLT-040).
    auto pos = resp.find("State changed to: ");
    if (pos != std::string::npos)
        currentState = resp.substr(pos + 18);
    return resp;
}

/**
 * @brief Sends authentication credentials to the server.
 *
 * Credentials are transmitted as a comma-separated @c "user,pass" payload.
 * On success (response contains @c "Authentication Successful"), transitions
 * @ref currentState to @c "PRE_FLIGHT".
 *
 * @param user Pilot username.
 * @param pass Pilot password.
 * @return The raw authentication response string from the server.
 */
std::string ClientApp::handleLogin(const std::string& user, const std::string& pass) {
    std::string resp = client.pkRequest(user + "," + pass, pkt_auth);
    if (resp.find("Authentication Successful") != std::string::npos)
        currentState = "PRE_FLIGHT";
    return resp;
}

/**
 * @brief Requests weather information for the specified location.
 *
 * @param location ICAO airport code or location name (e.g. @c "YYZ").
 * @return The weather data response string from the server.
 */
std::string ClientApp::handleWeather(const std::string& location) {
    return client.dataRequest(location, req_weather);
}

/**
 * @brief Requests the flight plan for the specified flight ID.
 *
 * @param flightId The flight identifier (e.g. @c "AC123").
 * @return The flight plan response string from the server.
 */
std::string ClientApp::handleFlight(const std::string& flightId) {
    return client.dataRequest(flightId, req_fplan);
}

/**
 * @brief Requests taxi or landing clearance from the server.
 *
 * Inspects the response to determine the resulting state transition:
 * - A @c "departure" keyword advances @ref currentState to @c "ACTIVE_AIRSPACE".
 * - An @c "arrival" keyword returns @ref currentState to @c "PRE_FLIGHT".
 *
 * @return The taxi/landing clearance response string from the server.
 */
std::string ClientApp::handleTaxi() {
    std::string resp = client.dataRequest("", req_taxi);
    if (resp.find("departure") != std::string::npos)
        currentState = "ACTIVE_AIRSPACE";
    else if (resp.find("arrival") != std::string::npos)
        currentState = "PRE_FLIGHT";
    return resp;
}

/**
 * @brief Requests landing clearance.
 *
 * Convenience wrapper around handleTaxi() — the server uses the same
 * endpoint for both taxi and landing clearance requests.
 *
 * @return The clearance response string from the server.
 */
std::string ClientApp::handleLand() {
    return handleTaxi();
}

/**
 * @brief Sends a telemetry update to the server.
 *
 * If @p data is non-empty it is forwarded directly.  Otherwise, simulated
 * GPS telemetry is generated with randomised values in realistic ranges:
 * - Latitude:  43.0 – 60.0 °N
 * - Longitude: 140.0 – 60.0 °W
 * - Altitude:  5,000 – 35,000 ft
 * - Speed:     200 – 500 kt
 *
 * The simulated payload is formatted as:
 * @code
 *   Lat=<val>,Long=<val>,Alt=<val>,Speed=<val>
 * @endcode
 *
 * @param data Optional pre-formatted telemetry string.  Pass an empty string
 *             to use the built-in GPS simulator.
 * @return The server's acknowledgement response string.
 */
std::string ClientApp::handleTelemetry(const std::string& data) {
    if (!data.empty())
        return client.dataRequest(data, req_telemetry);

    // Simulate GPS: randomized lat/long/alt/speed
    static std::mt19937 rng(std::random_device{}());
    auto randFloat = [&](float lo, float hi) {
        return lo + std::uniform_real_distribution<float>(0.f, 1.f)(rng) * (hi - lo);
    };
    auto randInt = [&](int lo, int hi) {
        return std::uniform_int_distribution<int>(lo, hi)(rng);
    };

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4)
       << "Lat="   << randFloat(43.0f, 60.0f)
       << ",Long=" << randFloat(-140.0f, -60.0f)
       << ",Alt="  << randInt(5000, 35000)
       << ",Speed="<< randInt(200, 500);

    return client.dataRequest(ss.str(), req_telemetry);
}

/**
 * @brief Requests current air-traffic information from the server.
 *
 * @return The air-traffic response string from the server.
 */
std::string ClientApp::handleTraffic() {
    return client.dataRequest("", req_traffic);
}

/**
 * @brief Requests a flight manual download from the server.
 *
 * The file is saved to @c ../client/flightmanual.pdf relative to the
 * executable's working directory.
 *
 * @return The server's response string (e.g. a confirmation or error message).
 */
std::string ClientApp::handleManualDownload() {
    return client.downloadFlightManual("../client/flightmanual.pdf");
}

// ---------------------------------------------------------
// Cleanup
// ---------------------------------------------------------
 
/**
 * @brief Disconnects from the server and releases Winsock resources.
 *
 * Safe to call multiple times — guards on @ref connected and @ref initialized
 * prevent double-cleanup.  Called automatically by the destructor.
 */
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
