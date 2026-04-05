/**
 * @file main.cpp
 * @brief Entry point for the client API bridge process.
 *
 * This executable acts as a thin stdin/stdout bridge between the Python UI
 * (clientUI.py) and the aviation backend server.  The UI spawns this process
 * as a child, writes newline-terminated command strings to its stdin, and reads
 * response strings from its stdout.
 *
 * Command format (space-delimited, read from stdin):
 * @code
 *   <CMD> [arg1] [arg2]
 * @endcode
 *
 * Response format (newline-terminated, written to stdout):
 * @code
 *   <response string from ClientApp::apiRequest>
 * @endcode
 */
#include <iostream>
#include <sstream>
#include "ClientApp.hpp"

/**
 * @brief Initialises the ClientApp and runs the stdin command-dispatch loop.
 *
 * Connects to the backend server, then repeatedly reads lines from stdin.
 * Each line is tokenised into up to three whitespace-delimited fields
 * (@p cmd, @p arg1, @p arg2) and forwarded to ClientApp::apiRequest().
 * The returned response string is written to stdout and flushed immediately
 * so that the parent process receives it without buffering delay.
 *
 * The loop exits cleanly when stdin is closed (e.g. the parent sends "QUIT"
 * and closes the pipe, or the process is terminated).
 *
 * @return 0 on normal exit.
 */
int main() {
    /// @brief Application instance connected to the backend at 127.0.0.1:8080.
    ClientApp app("127.0.0.1", 8080);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::stringstream ss(line);
        std::string cmd, arg1, arg2;
        ss >> cmd >> arg1 >> arg2;

        std::string response = app.apiRequest(cmd, arg1, arg2);
        std::cout << response << std::endl;
        std::cout.flush();
    }

    return 0;
}
