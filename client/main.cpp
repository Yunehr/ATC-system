/**
 * @file main_cli.cpp
 * @brief Entry point for the interactive CLI client (legacy / debug interface).
 *
 * Constructs a ClientApp and runs the blocking stdin command loop.  This
 * binary is intended for direct terminal use and debugging; the production
 * path uses the stdout-bridge entry point in @c main.cpp, which is driven
 * by the Python UI.
 *
 * @par Usage
 * @code
 * client_cli [ip] [port]
 * @endcode
 *
 * Both arguments are optional and fall back to defaults if omitted or invalid.
 */
#include "ClientApp.hpp"
#include <cstdlib>

/**
 * @brief Parses optional connection arguments, constructs a ClientApp, and
 *        runs the interactive command loop.
 *
 * @par Argument handling
 * - @p argv[1] : IPv4 address of the backend server.
 *                Defaults to @c "127.0.0.1" if not supplied.
 * - @p argv[2] : TCP port number (1–65535).
 *                Defaults to @c 54000 if not supplied or out of range.
 *                Out-of-range values parsed by @c std::atoi (including @c 0
 *                and negatives) are silently ignored and the default is kept.
 *
 * @param argc Number of command-line arguments (including the program name).
 * @param argv Array of command-line argument strings.
 * @return @c 0 on normal exit.
 */
int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    unsigned short port = 54000;

    if (argc >= 2) ip = argv[1];
    if (argc >= 3) {
        int p = std::atoi(argv[2]);
        if (p > 0 && p <= 65535) port = (unsigned short)p;
    }

    ClientApp app(ip, port);
    app.runCommandLoop();

    return 0;
}
