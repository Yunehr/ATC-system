/**
 * @file Logger.hpp
 * @brief Declaration of the Logger class.
 *
 * Provides a single static method, logPacket(), for appending packet
 * transmission records to @c ClientTransmitLog.csv.
 *
 * The log file path is resolved lazily on first use and the entry counter
 * is seeded from any pre-existing records, so log IDs remain unique across
 * process restarts.
 *
 * @par Requirement
 * REQ-SYS-050: The client application shall log all transmitted (TX) and
 * received (RX) data packets to @c ClientTransmitLog.csv.
 */
#pragma once
#include <string>
#include "../shared/Packet.h"

 
/**
 * @class Logger
 * @brief Static utility class for CSV packet logging (REQ-SYS-050).
 *
 * All methods are static; Logger is not intended to be instantiated.
 * Internal state (log file path, entry counter, initialisation flag) is
 * held in module-level statics in @c Logger.cpp and managed transparently
 * by the private initialize() method, which is called lazily on the first
 * logPacket() invocation.
 */
class Logger {
public:
    /**
     * @enum Direction
     * @brief Indicates whether a packet was transmitted or received.
     */
    enum class Direction { TX, RX };
    /**
     * @brief Appends a single packet log entry to the CSV file.
     *
     * Initialises the logger on first call (lazy).  Sanitises @p description
     * by replacing newline characters with spaces, then writes one CSV row in
     * the format:
     * @code
     * LogID, ClientID, Direction, PacketType, Timestamp, Description
     * @endcode
     * Silently returns without writing if the log file cannot be opened.
     *
     * @param pkt         The packet to log; supplies the client ID and packet type.
     * @param dir         Transmission direction: @c Direction::TX or @c Direction::RX.
     * @param description Human-readable context string (e.g. @c "Data request type 2").
     */
    static void logPacket(packet& pkt, Direction dir, const std::string& description);

private:
    /**
     * @brief Resolves the log file path and seeds the entry counter (lazy, idempotent).
     *
     * Creates the CSV file with a header row if it does not yet exist.
     * If the file already exists, reads the highest @c LogID present so that
     * IDs continue from the previous session without repeating.
     */
    static int         nextId();
    /**
     * @brief Increments and returns the next unique log entry ID.
     * @return Next sequential log ID.
     */    
    static std::string currentTimestamp();
    /**
     * @brief Returns the current local time as a @c "YYYY-MM-DD HH:MM:SS" string.
     * @return Formatted timestamp string.
     */    
    static std::string packetTypeName(int pkType);
    /**
     * @brief Maps a numeric packet type to its CSV label string.
     *
     * @param pkType Numeric packet type (a @c pkTyFl enumeration value).
     * @return One of @c "EMPTY", @c "REQUEST", @c "DATA", @c "AUTH",
     *         @c "EMERGENCY", or @c "UNKNOWN".
     */
    static void        initialize();
};
