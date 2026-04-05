#include "Logger.hpp"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

// --- module-level state -------------------------------------------------------

static int         s_logId       = 0;
static bool        s_initialized = false;
static std::string s_logPath;

// Probe for the data directory using the same file the server uses,
// then pick the client-specific log file in the same directory.
static std::string detectLogPath() {
    std::ifstream probe("../data/SystemTrafficLog.csv");
    return probe.is_open() ? "../data/ClientTransmitLog.csv"
                           : "data/ClientTransmitLog.csv";
}

void Logger::initialize() {
    if (s_initialized) return;
    s_initialized = true;
    s_logPath = detectLogPath();

    std::ifstream in(s_logPath);
    if (!in.is_open()) {
        // File doesn't exist yet — create it with the header row.
        std::ofstream hdr(s_logPath);
        if (hdr.is_open())
            hdr << "LogID,ClientID,Direction,PacketType,Timestamp,Description\n";
        return;
    }

    // Seed counter from highest LogID already in the file so IDs never repeat.
    std::string line;
    std::getline(in, line); // skip header
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string idStr;
        if (std::getline(ss, idStr, ',')) {
            try {
                int v = std::stoi(idStr);
                if (v > s_logId) s_logId = v;
            } catch (...) {}
        }
    }
}

// --- private helpers ----------------------------------------------------------

int Logger::nextId() {
    return ++s_logId;
}

std::string Logger::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string Logger::packetTypeName(int pkType) {
    switch (pkType) {
    case pkt_empty: return "EMPTY";
    case pkt_req:   return "REQUEST";
    case pkt_dat:   return "DATA";
    case pkt_auth:  return "AUTH";
    case pkt_emgcy: return "EMERGENCY";
    default:        return "UNKNOWN";
    }
}

// --- public API ---------------------------------------------------------------

void Logger::logPacket(packet& pkt, Direction dir, const std::string& description) {
    initialize();

    std::ofstream out(s_logPath, std::ios::app);
    if (!out.is_open()) return;

    std::string desc = description;
    for (char& c : desc)
        if (c == '\n' || c == '\r') c = ' ';

    out << nextId()                                       << ","
        << (int)pkt.getCLID()                            << ","
        << (dir == Direction::RX ? "RX" : "TX")         << ","
        << packetTypeName(pkt.getPKType())               << ","
        << currentTimestamp()                            << ","
        << "\"" << desc << "\"\n";
}
