/**
 * @file ClientApp.hpp
 * @brief Declaration of the ClientApp class.
 *
 * ClientApp is the central controller for the aviation client.  It owns the
 * Winsock lifecycle and a ClientEngine socket connection, and exposes a
 * uniform apiRequest() dispatcher used by both the legacy interactive CLI
 * (runCommandLoop()) and the Python stdin bridge (main.cpp).
 *
 * @par State machine
 * ClientApp mirrors the server-side state machine through @ref currentState
 * (REQ-CLT-040).  Valid states are:
 * | State           | Meaning                                       |
 * |-----------------|-----------------------------------------------|
 * | STARTUP/AUTH    | Initial state; awaiting successful login.     |
 * | PRE_FLIGHT      | Authenticated; pre-departure checks active.   |
 * | ACTIVE_AIRSPACE | Taxi clearance granted; aircraft in airspace. |
 *
 * @par Future work
 * A pybind11 binding layer is planned to expose individual handlers as a
 * native Python module, replacing the current stdin/stdout bridge.
 */
#pragma once
#include "ClientEngine.hpp"
#include <winsock2.h>
#include <string>

#pragma comment(lib, "ws2_32.lib")

 
/**
 * @class ClientApp
 * @brief Manages the Winsock connection and all aviation-domain request handlers.
 *
 * Responsibilities:
 * - Winsock initialisation and teardown (WSAStartup / WSACleanup).
 * - Opening and closing the TCP connection via ClientEngine.
 * - Dispatching commands received from the Python UI to the appropriate handler.
 * - Tracking the current server-side state (REQ-CLT-040).
 */
class ClientApp {
public:
 /**
    * @brief Constructs a ClientApp, initialises Winsock, and connects to the server.
    *
    * Writes @c "CONNECTED\\n" to stdout on success, or @c "CONNECT_FAILED\\n" /
    * @c "WSAStartup failed\\n" on failure.  Check isConnected() before use.
    *
    * @param ip   IPv4 address of the backend server. Defaults to @c "127.0.0.1".
    * @param port TCP port of the backend server.    Defaults to @c 54000.
    */
    ClientApp(const std::string& ip = "127.0.0.1", unsigned short port = 54000);

    /**
    * @brief Destructor — calls shutdown() to release all resources.
    */

    ~ClientApp();
    /**
    * @brief Runs the legacy interactive stdin command loop.
    *
    * Reads whitespace-delimited tokens from stdin and dispatches them to
    * the appropriate handler, printing each response to stdout.  Exits on
    * @c "QUIT" or stdin EOF.
    *
    * @note This loop is retained for debugging.  The production entry point
    *       is apiRequest(), driven by the Python bridge in main.cpp.
    */
    void runCommandLoop();

    /**
    * @brief Disconnects from the server and releases Winsock resources.
    *
    * Safe to call multiple times.  Also invoked automatically by the destructor.
    */

    void shutdown();

    // -------------------------------------------------------------------------
    // Primary API dispatcher
    // -------------------------------------------------------------------------
 
   /**
    * @brief Dispatches a command string to the appropriate handler.
    *
    * Called once per line by the stdin bridge in main.cpp.
    *
    * @param cmd  Command token. Recognised values:
    *             @c EMERGENCY, @c RESOLVE, @c LOGIN, @c WEATHER, @c FLIGHT,
    *             @c TAXI, @c LAND, @c TELEM, @c TRAFFIC, @c MANUAL.
    * @param arg1 First optional argument (e.g. username, airport code, flight ID).
    * @param arg2 Second optional argument (e.g. password for @c LOGIN).
    * @return Response string to forward to the Python UI, or
    *         @c "ERROR_NOT_CONNECTED" if the socket is not ready, or
    *         @c "ERROR_UNKNOWN_CMD" if @p cmd is not recognised.
    */
    std::string apiRequest(const std::string& cmd, const std::string& arg1, const std::string& arg2);
    // -------------------------------------------------------------------------
    // Individual request handlers (public so they can be bound via pybind11)
    // -------------------------------------------------------------------------
 
    /**
    * @brief Declares an in-flight emergency to the server.
    *
    * Parses the @c "State changed to: " substring and updates @ref currentState
    * (REQ-CLT-040).
    *
    * @return Raw server response string.
    */
    std::string handleEmergency();
    /**
    * @brief Resolves an in-flight emergency with the server.
    *
    * On a successful response (containing @c "Emergency resolved"), parses the
    * new state from the @c "State changed to: " substring and updates
    * @ref currentState accordingly.
    *
    * @return Raw server response string.
    */
    std::string handleLogin(const std::string& user, const std::string& pass);
    /**
     * @brief Requests weather data for the given location.
     *
     * @param location ICAO airport code or location name (e.g. @c "YYZ").
     * @return Weather data response string from the server.
     */
    std::string handleWeather(const std::string& location);
    /**
    * @brief Requests the flight plan for the given flight ID.
    *
    * @param flightId Flight identifier (e.g. @c "AC123").
    * @return Flight plan response string from the server.
    */
    std::string handleFlight(const std::string& flightId);
     
    /**
    * @brief Requests taxi or landing clearance from the server.
    *
    * Transitions @ref currentState to @c "ACTIVE_AIRSPACE" on a departure
    * clearance, or back to @c "PRE_FLIGHT" on an arrival clearance.
    *
    * @return Clearance response string from the server.
    */
    std::string handleTaxi();
     
    /**
    * @brief Requests landing clearance.
    *
    * Convenience wrapper around handleTaxi() — the server uses the same
    * endpoint for both taxi and landing requests.
    *
    * @return Clearance response string from the server.
    */
    std::string handleLand();
    
    /**
    * @brief Sends a telemetry update to the server.
    *
    * If @p data is non-empty it is forwarded directly.  Otherwise, simulated
    * GPS telemetry is generated with randomised values (lat, long, alt, speed).
    *
    * @param data Pre-formatted telemetry string, or empty to use the simulator.
    * @return Server acknowledgement response string.
    */
    std::string handleTelemetry(const std::string& data = "");
     
    /**
    * @brief Requests current air-traffic information from the server.
    *
    * @return Air-traffic response string from the server.
    */
    std::string handleTraffic();
 
    /**
    * @brief Requests a flight manual download from the server.
    *
    * The file is saved to @c ../client/flightmanual.pdf relative to the
    * executable's working directory.
    *
    * @return Server response string confirming the download or describing an error.
    */    
    std::string handleManualDownload();
    std::string handleResolveEmergency();

private:
    /// @brief IPv4 address of the backend server.
    std::string ip;
    /// @brief TCP port of the backend server.    
    unsigned short port;
    /// @brief Winsock data structure populated by WSAStartup.    
    WSADATA wsa;
    /// @brief Underlying socket wrapper that performs all network I/O.
    ClientEngine client;

    /// @brief True after WSAStartup succeeds; guards WSACleanup in shutdown().
    bool initialized;
    /// @brief True after connectToServer succeeds; guards disconnect() in shutdown() and apiRequest().
    bool connected;
  /**
    * @brief Last known server-side state (REQ-CLT-040).
    *
    * Updated by handleLogin(), handleTaxi(), handleEmergency(), and
    * handleResolveEmergency() whenever the server reports a state transition.
    */
    std::string currentState; 
};
