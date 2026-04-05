/**
 * @file FileReceiver.hpp
 * @brief Declaration of the FileReceiver class.
 *
 * FileReceiver manages the write-side of the multi-packet file transfer
 * protocol used to download the flight manual.  It assembles incoming
 * @c pkt_dat packets into a @c .part staging file and atomically renames
 * it to the final path once the server signals transfer completion.
 *
 * @par Typical usage
 * @code
 * FileReceiver receiver;
 * std::string err;
 *
 * if (!receiver.start("output/flightmanual.pdf", err)) { ... }
 *
 * while (!receiver.isComplete()) {
 *     packet pkt = ...; // receive from PacketTransport
 *     if (!receiver.processPacket(pkt, err)) { ... }
 * }
 *
 * if (!receiver.finalize(err)) { ... }
 * @endcode
 */
 
#pragma once

#include <fstream>
#include <string>
#include "../shared/Packet.h"

/**
 * @class FileReceiver
 * @brief Assembles a multi-packet file transfer into a local file.
 *
 * Each packet payload begins with a 4-byte little-endian write offset,
 * followed by the file data for that chunk.  This allows packets to arrive
 * out of order without corrupting the output.
 *
 * The file is first written to a @c .part staging path.  finalize() flushes,
 * closes, and atomically renames it to the final path, ensuring no truncated
 * file is ever left at the destination on failure or interruption.
 *
 * FileReceiver is owned and driven by ClientEngine::downloadFlightManual().
 */
class FileReceiver {
public:

    /**
    * @brief Constructs a FileReceiver in its initial idle state.
    *
    * No file is opened until start() is called.
    */
	FileReceiver();
	/**
    * @brief Destructor — closes the staging file if it is still open.
    *
    * Does not finalize or rename the staging file.
    */
	~FileReceiver();

 
    // -------------------------------------------------------------------------
    // Transfer lifecycle
    // -------------------------------------------------------------------------
 
    /**
    * @brief Opens the staging file and resets receiver state for a new transfer.
    *
    * The staging file is created at @c <outputPath>.part.  Any previously
    * open file is closed before the new one is opened.
    *
    * @param outputPath Desired final path of the downloaded file.
    * @param error      Populated with a human-readable message on failure.
    * @return @c true  if the staging file was opened successfully.
    * @return @c false if the staging file could not be opened; @p error is set.
    */	
	bool start(const std::string& outputPath, std::string& error);

	/**
    * @brief Writes the payload of a single @c pkt_dat packet into the staging file.
    *
    * Reads the 4-byte write offset from the start of the payload, seeks to
    * that position, and writes the remaining bytes.  Updates @ref bytesReceived
    * to the high-water mark of all write positions.
    *
    * Sets the receiver complete if the packet's transmission flag is
    * @c PKT_TRNSMT_COMP.
    *
    * @param pak   The received packet. Must be of type @c pkt_dat.
    * @param error Populated with a human-readable message on failure.
    * @return @c true  if the packet was written successfully.
    * @return @c false on type mismatch, invalid payload, or I/O error;
    * @p error is set.
    */
	bool processPacket(packet& pak, std::string& error);
    /**
    * @brief Flushes, closes, and renames the staging file to the final path.
    *
    * Any pre-existing file at the final path is removed before the rename.
    * Must only be called after isComplete() returns @c true.
    *
    * @param error Populated with a human-readable message on failure.
    * @return @c true  if the file was finalized successfully.
    * @return @c false if the transfer was incomplete or the rename failed;
    *                  @p error is set.
    */
	bool finalize(std::string& error);
 
    // -------------------------------------------------------------------------
    // Accessors
    // -------------------------------------------------------------------------
 
    /**
    * @brief Returns whether the final packet of the transfer has been received.
    *
    * Set to @c true by processPacket() when a @c PKT_TRNSMT_COMP packet is
    * processed.  Does not imply the file has been finalized.
    *
    * @return @c true if the transfer is complete.
    */
	bool isComplete() const;
    /**
    * @brief Returns the high-water mark of byte positions written to the staging file.
    *
    * Suitable for progress reporting (e.g. @c getBytesReceived() / 1024 KB).
    * Reflects the furthest byte offset written, not a simple cumulative count.
    *
    * @return Highest byte offset written so far, in bytes.
    */
	uint32_t getBytesReceived() const;

private:
    /// @brief Final destination path of the downloaded file.
	std::string finalPath;

    /// @brief Path of the in-progress staging file (@c finalPath + @c ".part").
	std::string tempPath;
    /// @brief File stream used to write the staging file.
	std::fstream out;
    /// @brief @c true once a packet with @c PKT_TRNSMT_COMP has been processed.
	bool complete;
    /// @brief High-water mark of byte positions written to the staging file.
	uint32_t bytesReceived;
};
