/**
 * @file main.cpp
 * @brief Entry point for the aviation backend server process.
 *
 * Initialises Winsock, starts the ServerEngine on a fixed port, and enters
 * the blocking client-handling loop.  Winsock is cleaned up on both normal
 * and error exit paths.
 */
#include "ServerEngine.hpp"
#include <winsock2.h>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")
/**
 * @brief Initialises Winsock, binds the server, and runs the accept loop.
 *
 * Performs the following steps in order:
 * -# WSAStartup — initialises Winsock 2.2.  Returns @c 1 on failure.
 * -# ServerEngine::start() — binds and begins listening on port @c 8080.
 *    Calls WSACleanup() and returns @c 1 on failure.
 * -# ServerEngine::run() — blocks indefinitely, accepting and servicing
 *    client connections.
 * -# WSACleanup() — releases Winsock resources before returning.
 *
 * @return @c 0 on clean shutdown, @c 1 if Winsock initialisation or server
 *         startup fails.
 */
int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        std::cout << "WSAStartup failed\n";
        return 1;
    }

    ServerEngine server;
    if (!server.start(8080)) {
        WSACleanup();
        return 1;
    }

    server.run();

    WSACleanup();
    return 0;
}