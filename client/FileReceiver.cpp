#include "FileReceiver.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>

FileReceiver::FileReceiver() : complete(false) {}

FileReceiver::~FileReceiver() {
	if (out.is_open()) {
		out.close();
	}
}

bool FileReceiver::start(const std::string& outputPath, std::string& error) {
	finalPath = outputPath;
	tempPath = outputPath + ".part";
	complete = false;

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
	}

	if (pak.getTFlag() == PKT_TRNSMT_COMP) {
		complete = true;
	}

	return true;
}

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

bool FileReceiver::isComplete() const {
	return complete;
}
