/**
 * @file Logger.cpp
 * @brief Implementation of the server-side Logger class.
 *
 * Appends one CSV row per packet to @c TransmitReceiveLog.csv.  The log
 * file path is detected at runtime by probing for the shared @c data/
 * directory.  LogIDs are seeded from the highest ID already present in the
 * file so that IDs remain unique across server restarts.
 *
 * @par CSV column layout
 * @code
 * LogID, ClientID, Direction, PacketType, ServerState, Timestamp, Description
 * @endcode
 *
 * @note This is the server-side logger.  The client-side equivalent writes
 *       to @c ClientTransmitLog.csv and does not include a @c ServerState column.
 */
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <ctime>

// ---------------------------------------------------------
// Module-level state
// ---------------------------------------------------------
 
/// @brief Monotonically increasing log entry counter; seeded on first use.
static int         s_logId        = 0;

/// @brief Guards against repeated initialisation across multiple logPacket() calls.
static bool        s_initialized  = false;
/// @brief Resolved path to the CSV log file, set by detectLogPath().
static std::string s_logPath;

 
// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Resolves the CSV log file path by probing for the shared data directory.
 *
 * Attempts to open @c SystemTrafficLog.csv as a location probe (the same
 * file used by FileTransferManager).  If it exists the server log is placed
 * in the same @c ../data/ directory; otherwise a local @c data/ directory
 * is assumed (e.g. when running from the build output directory).
 *
 * @return Relative path string to @c TransmitReceiveLog.csv.
 */
static std::string detectLogPath() {
    std::ifstream probe("../data/SystemTrafficLog.csv");
    return probe.is_open() ? "../data/TransmitReceiveLog.csv"
                           : "data/TransmitReceiveLog.csv";
}

 
// ---------------------------------------------------------
// Initialisation
// ---------------------------------------------------------
 
/**
 * @brief Initialises the logger on first use (lazy, idempotent).
 *
 * Resolves the log file path via detectLogPath().  If the file does not yet
 * exist it is created with the CSV header row (including the @c ServerState
 * column absent from the client-side log).  If it already exists the highest
 * @c LogID present is read so that the counter continues from the previous
 * session, ensuring IDs never repeat across restarts.
 *
 * Subsequent calls are no-ops (@ref s_initialized guards re-entry).
 */
void Logger::initialize() {
    if (s_initialized) return;
    s_initialized = true;
    s_logPath = detectLogPath();

    std::ifstream in(s_logPath);
    if (!in.is_open()) {
        // File doesn't exist yet, create it with the header row.
        std::ofstream hdr(s_logPath);
        if (hdr.is_open())
            hdr << "LogID,ClientID,Direction,PacketType,ServerState,Timestamp,Description\n";
        return;
    }

    // Seed the counter from the highest LogID already in the file.
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

// ---------------------------------------------------------
// Private helpers
// ---------------------------------------------------------
 
/**
 * @brief Increments and returns the next unique log entry ID.
 *
 * @return The next sequential log ID (1-based after a fresh start).
 */

int Logger::nextId() {
    return ++s_logId;
}

/**
 * @brief Returns the current local time formatted as @c "YYYY-MM-DD HH:MM:SS".
 *
 * @return Timestamp string for use in a CSV log row.
 */
std::string Logger::currentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

/**
 * @brief Maps a numeric packet type value to its human-readable CSV label.
 *
 * @param pkType Numeric packet type (one of the @c pkTyFl enumeration values).
 * @return String label for the @c PacketType CSV column.
 *         Returns @c "UNKNOWN" for any unrecognised value.
 */
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

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------
 
/**
 * @brief Appends a single packet log entry to the CSV file.
 *
 * Calls initialize() on first use.  The @p description string is sanitised
 * by replacing newline characters with spaces before writing.  The
 * description is double-quoted in the output to allow commas within the text.
 *
 * Unlike the client-side logger, this method also records @p serverState,
 * providing a full audit trail of which server state was active when each
 * packet was processed.
 *
 * @par Output row format
 * @code
 * <LogID>,<ClientID>,<RX|TX>,<PacketType>,<ServerState>,<YYYY-MM-DD HH:MM:SS>,"<description>"
 * @endcode
 *
 * Silently returns without writing if the log file cannot be opened for append.
 *
 * @param pkt         The packet to log; supplies the client ID and packet type.
 * @param dir         Transmission direction: @c Direction::RX or @c Direction::TX.
 * @param serverState The server's current state string at the time of logging
 *                    (e.g. @c "PRE_FLIGHT", @c "ACTIVE_AIRSPACE").
 * @param description Human-readable context string (e.g. @c "Auth request from client 1").
 */
void Logger::logPacket(packet& pkt, Direction dir,
                       const std::string& serverState,
                       const std::string& description) {
    initialize();

    std::ofstream out(s_logPath, std::ios::app);
    if (!out.is_open()) return;

    // Sanitize description: commas are wrapped in quotes; newlines replaced.
    std::string desc = description;
    for (char& c : desc)
        if (c == '\n' || c == '\r') c = ' ';

    out << nextId()                                       << ","
        << (int)pkt.getCLID()                            << ","
        << (dir == Direction::RX ? "RX" : "TX")         << ","
        << packetTypeName(pkt.getPKType())               << ","
        << serverState                                   << ","
        << currentTimestamp()                            << ","
        << "\"" << desc << "\"\n";
}
