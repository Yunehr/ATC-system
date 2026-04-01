#include "ClientApp.hpp"
#include <cstdlib>

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
