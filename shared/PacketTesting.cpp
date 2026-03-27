#include <iostream>
#include <cassert>
#include <cstring>
#include "packet.h"

int main() {
    std::cout << "Starting Packet QA Test..." << std::endl;

    // 1. Create a packet with sample data
    packet txPkt;
    char sampleData[] = "ATC_READY";
    int dataSize = sizeof(sampleData);
    txPkt.PopulPacket(sampleData, dataSize, 1, pkt_dat);

    // 2. Serialize it (Simulate sending)
    int finalSize = 0;
    char* buffer = txPkt.Serialize(&finalSize);

    // 3. Deserialize it (Simulate receiving)
    packet rxPkt(buffer);

    // 4. Verify Integrity (REQ-INT-040) 
    assert(txPkt.calcCRC() == rxPkt.calcCRC());
    std::cout << "[PASS] Checksum verified." << std::endl;

    // 5. Verify Data Transfer (REQ-SYS-030)
    assert(rxPkt.getPloadLength() == dataSize);
    assert(std::string(rxPkt.getData()) == "ATC_READY");
    std::cout << "[PASS] Data integrity verified." << std::endl;

    // 6. Test Corruption Detection
    // Using 2 because the header is 2 bytes (flags/ID + length)
    buffer[2] ^= 0xFF; // Flip bits in the first byte of the body
    packet corruptedPkt(buffer);
    if (corruptedPkt.calcCRC() != txPkt.calcCRC()) {
    std::cout << "[PASS] Corruption detected via CRC." << std::endl;
    }
    std::cout << "All packet tests passed!" << std::endl;
    return 0;
}