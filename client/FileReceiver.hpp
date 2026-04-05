#pragma once

#include <fstream>
#include <string>
#include "../shared/Packet.h"

class FileReceiver {
public:
	FileReceiver();
	~FileReceiver();

	bool start(const std::string& outputPath, std::string& error);
	bool processPacket(packet& pak, std::string& error);
	bool finalize(std::string& error);
	bool isComplete() const;
	uint32_t getBytesReceived() const;

private:
	std::string finalPath;
	std::string tempPath;
	std::fstream out;
	bool complete;
	uint32_t bytesReceived;
};
