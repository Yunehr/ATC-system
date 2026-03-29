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

private:
	std::string finalPath;
	std::string tempPath;
	std::fstream out;
	bool complete;
};
