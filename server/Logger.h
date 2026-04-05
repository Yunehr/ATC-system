#pragma once
#include <string>
#include "../shared/Packet.h"

// REQ-LOG-010: Log every transmitted (TX) and received (RX) packet.
// REQ-LOG-020: Each entry gets a unique, ever-increasing LogID.
// REQ-LOG-030: Each entry carries a direction flag (TX/RX) and a human-readable description.
// REQ-LOG-040: Telemetry entries include client status and last-known location from the packet body.
//
// Output: data/TransmitReceiveLog.csv
class Logger {
public:
    enum class Direction { TX, RX };

    static void logPacket(packet& pkt, Direction dir,
                          const std::string& serverState,
                          const std::string& description);

private:
    static int         nextId();
    static std::string currentTimestamp();
    static std::string packetTypeName(int pkType);
    static void        initialize();
};
