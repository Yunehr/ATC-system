/**
 * @file FileReceiver.cpp
 * @brief Implementation of the FileReceiver class.
 *
 * Manages the write-side of the multi-packet file transfer protocol used to
 * download the flight manual.  Each packet carries a 4-byte little-endian
 * write offset followed by file data, allowing out-of-order delivery.
 * The file is first assembled into a @c .part staging file and atomically
 * renamed to its final path once the server signals transfer completion.
 */
#include "FileReceiver.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>

// ---------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------
 
/**
* @brief Constructs a FileReceiver in its initial idle state.
*
* No file is opened until start() is called.
*/
FileReceiver::FileReceiver() : complete(false), bytesReceived(0) {}

/**
 * @brief Destructor — ensures the staging file is closed if still open.
 *
 * Does not finalize or rename the staging file; that is the responsibility
 * of the caller via finalize().
 */
FileReceiver::~FileReceiver() {
	if (out.is_open()) {
		out.close();
	}
}

// ---------------------------------------------------------
// Transfer lifecycle
// ---------------------------------------------------------
 
/**
 * @brief Opens the staging file and prepares the receiver for a new transfer.
 *
 * The incoming file is written to @c <outputPath>.part to avoid leaving a
 * truncated file at the final path if the transfer fails or is interrupted.
 * finalize() atomically renames the staging file to @p outputPath on success.
 *
 * If a staging file is already open from a previous session it is closed
 * before the new one is opened.
 *
 * @param outputPath Desired final path of the downloaded file
 *                   (e.g. @c "../client/flightmanual.pdf").
 * @param error      Populated with a human-readable message on failure.
 * @return @c true  if the staging file was opened successfully.
 * @return @c false if the staging file could not be opened; @p error is set.
 */
bool FileReceiver::start(const std::string& outputPath, std::string& error) {
	finalPath = outputPath;
	tempPath = outputPath + ".part";
	complete = false;
	bytesReceived = 0;

	if (out.is_open()) {
		out.close();
	}

	out.open(tempPath, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
	if (!out.is_open()) {
		error = "Failed to open staging file: " + tempPath;
		return false;
	}

	return true;
}

/**
 * @brief Writes the payload of a single data packet into the staging file.
 *
 * @par Packet payload layout
 * @code
 * [ 4 bytes: uint32_t write offset ] [ N bytes: file data ]
 * @endcode
 * The offset is applied via seekp() before writing, so packets may arrive
 * out of order without corrupting the output.
 *
 * @ref bytesReceived is updated to track the highest byte position written,
 * giving an accurate progress figure even with out-of-order packets.
 *
 * Sets @ref complete to @c true if the packet's transmission flag is
 * @c PKT_TRNSMT_COMP, signalling that no further chunks will follow.
 *
 * @param pak   The received packet to process.  Must be of type @c pkt_dat.
 * @param error Populated with a human-readable message on failure.
 * @return @c true  if the packet was written successfully.
 * @return @c false on type mismatch, invalid payload size, seek failure, or
 *                  write failure; @p error is set.
 */
bool FileReceiver::processPacket(packet& pak, std::string& error) {
	if (pak.getPKType() != pkt_dat) {
		error = "Unexpected packet type during manual transfer";
		return false;
	}

	if (pak.getPloadLength() < sizeof(uint32_t)) {
		error = "Invalid packet payload size for file transfer";
		return false;
	}

	const char* payload = pak.getData();
	uint32_t offset = 0;
	std::memcpy(&offset, payload, sizeof(uint32_t));

	const int fileDataSize = (int)pak.getPloadLength() - (int)sizeof(uint32_t);
	if (fileDataSize > 0) {
		out.seekp((std::streamoff)offset, std::ios::beg);
		if (!out.good()) {
			error = "Failed to seek staging file";
			return false;
		}

		out.write(payload + sizeof(uint32_t), fileDataSize);
		if (!out.good()) {
			error = "Failed to write staging file";
			return false;
		}

		uint32_t positionAfterWrite = offset + (uint32_t)fileDataSize;
		if (positionAfterWrite > bytesReceived)
			bytesReceived = positionAfterWrite;
	}

	if (pak.getTFlag() == PKT_TRNSMT_COMP) {
		complete = true;
	}

	return true;
}

/**
 * @brief Flushes and closes the staging file, then atomically renames it to the final path.
 *
 * Any pre-existing file at @ref finalPath is removed before the rename so
 * that @c std::filesystem::rename() succeeds on platforms that do not
 * support atomic replacement.
 *
 * @note Must only be called after isComplete() returns @c true.
 *
 * @param error Populated with a human-readable message on failure.
 * @return @c true  if the file was finalized and renamed successfully.
 * @return @c false if the transfer was incomplete, or if the rename failed;
 *                  @p error is set.
 */
bool FileReceiver::finalize(std::string& error) {
	if (!complete) {
		error = "Transfer did not complete";
		return false;
	}

	out.flush();
	out.close();

	std::error_code ec;
	std::filesystem::remove(finalPath, ec);
	ec.clear();
	std::filesystem::rename(tempPath, finalPath, ec);
	if (ec) {
		error = "Failed to finalize file: " + ec.message();
		return false;
	}

	return true;
}

// ---------------------------------------------------------
// Accessors
// ---------------------------------------------------------
 
/**
 * @brief Returns whether the transfer has been marked complete.
 *
 * Set to @c true by processPacket() when a packet with @c PKT_TRNSMT_COMP
 * is received.  Does not imply the file has been finalized.
 *
 * @return @c true if the final packet of the transfer has been processed.
 */
bool FileReceiver::isComplete() const {
	return complete;
}

/**
 * @brief Returns the highest byte offset written to the staging file so far.
 *
 * Updated after every successful processPacket() call.  Suitable for
 * progress reporting (e.g. @c bytesReceived / 1024 KB).
 *
 * @return Number of bytes received, as the high-water mark of write positions.
 */
uint32_t FileReceiver::getBytesReceived() const {
	return bytesReceived;
}
