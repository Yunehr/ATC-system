#pragma once
#include <string>
#include "../shared/Packet.h"

// REQ-SYS-050: Client application logs all transmitted (TX) and received (RX)
// data packets to ClientTransmitLog.csv.
class Logger {
public:
    enum class Direction { TX, RX };

    static void logPacket(packet& pkt, Direction dir, const std::string& description);

private:
    static int         nextId();
    static std::string currentTimestamp();
    static std::string packetTypeName(int pkType);
    static void        initialize();
};
