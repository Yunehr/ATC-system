/**
 * @file Logger.h
 * @brief Declaration of the server-side Logger class.
 *
 * Provides a single static method, logPacket(), for appending packet
 * transmission and reception records to @c TransmitReceiveLog.csv.
 *
 * Unlike the client-side logger, each entry also captures the server's
 * current state at the time of logging, producing a full audit trail of
 * state-packet interactions.
 *
 * @par Requirements
 * - REQ-LOG-010: Every transmitted (TX) and received (RX) packet is logged.
 * - REQ-LOG-020: Each entry receives a unique, monotonically increasing LogID.
 * - REQ-LOG-030: Each entry carries a direction flag (TX/RX) and a
 *                human-readable description.
 * - REQ-LOG-040: Telemetry entries include client status and last-known
 *                location extracted from the packet body.
 *
 * @par Output
 * @c data/TransmitReceiveLog.csv with columns:
 * @code
 * LogID, ClientID, Direction, PacketType, ServerState, Timestamp, Description
 * @endcode
 */
#pragma once
#include <string>
#include "../shared/Packet.h"

/**
 * @class Logger
 * @brief Static utility class for server-side CSV packet logging.
 *
 * All methods are static; Logger is not intended to be instantiated.
 * Internal state (log file path, entry counter, initialisation flag) is
 * held in module-level statics in @c Logger.cpp and managed transparently
 * by the private initialize() method, which is called lazily on the first
 * logPacket() invocation.
 *
 * @see FileTransferManager::logTelemetry() for telemetry-specific logging
 *      to @c SystemTrafficLog.csv (REQ-LOG-040).
 */
class Logger {
public:
    /**
     * @enum Direction
     * @brief Indicates whether a packet was transmitted or received.
     */
    enum class Direction { TX, RX };

     
    /**
     * @brief Appends a single packet log entry to @c TransmitReceiveLog.csv.
     *
     * Initialises the logger on first call (lazy).  Sanitises @p description
     * by replacing newline characters with spaces, then writes one CSV row in
     * the format:
     * @code
     * LogID, ClientID, Direction, PacketType, ServerState, Timestamp, Description
     * @endcode
     * Silently returns without writing if the log file cannot be opened.
     *
     * @param pkt         The packet to log; supplies the client ID and packet type.
     * @param dir         Transmission direction: @c Direction::TX or @c Direction::RX.
     * @param serverState The server's current state string at the time of logging
     *                    (e.g. @c "PRE_FLIGHT", @c "ACTIVE_AIRSPACE").
     * @param description Human-readable context string
     *                    (e.g. @c "Auth request from client 1").
     */
    static void logPacket(packet& pkt, Direction dir,
                          const std::string& serverState,
                          const std::string& description);

private:
    /**
     * @brief Resolves the log file path and seeds the entry counter (lazy, idempotent).
     *
     * Creates @c TransmitReceiveLog.csv with a header row if it does not yet
     * exist.  If the file already exists, reads the highest @c LogID present
     * so that IDs continue from the previous session without repeating
     * (REQ-LOG-020).
     */
    static void initialize();
     /**
     * @brief Increments and returns the next unique log entry ID (REQ-LOG-020).
     * @return Next sequential log ID.
     */
    static int         nextId();
 
    /**
     * @brief Returns the current local time as a @c "YYYY-MM-DD HH:MM:SS" string.
     * @return Formatted timestamp string.
     */
    static std::string currentTimestamp();
     
    /**
     * @brief Maps a numeric packet type to its CSV label string.
     *
     * @param pkType Numeric packet type (a @c pkTyFl enumeration value).
     * @return One of @c "EMPTY", @c "REQUEST", @c "DATA", @c "AUTH",
     *         @c "EMERGENCY", or @c "UNKNOWN".
     */
    static std::string packetTypeName(int pkType);

};
