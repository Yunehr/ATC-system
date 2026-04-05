#include <iostream>
#include <sstream>
#include "ClientApp.hpp"

int main() {
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
