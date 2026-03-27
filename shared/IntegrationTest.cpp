#include <iostream>
#include <cassert>
#include <string>
#include "packet.h"
#include "request.h"

int main() {
    std::cout << "Starting Integration QA Test..." << std::endl;

    // 1. Create a Weather Request (REQ-SVR-040) [cite: 53-54, 497-498]
    char weatherData[] = "CYKF_CLEAR_15C"; 
    int reqSize = sizeof(weatherData) - 1; // Correctly ignore null terminator
    Request txReq(req_weather, reqSize, weatherData);

    // 2. Wrap Request in a Packet (REQ-SYS-020) 
    int serializedSize = 0;
    char* serializedReq = txReq.serializeRequest(&serializedSize);
    
    packet txPkt;
    txPkt.PopulPacket(serializedReq, serializedSize, 1, pkt_req);

    // 3. Simulate Serialization for Transport (REQ-COM-010) [cite: 420-422, 613]
    int totalBytes = 0;
    char* wireBuffer = txPkt.Serialize(&totalBytes);

    // 4. Simulate Receiving and Rebuilding (REQ-INT-040) [cite: 430-431, 612]
    packet rxPkt(wireBuffer);
    
    // Verify CRC before processing
    assert(txPkt.calcCRC() == rxPkt.calcCRC());
    std::cout << "[PASS] Integration CRC Checksum verified." << std::endl;

    // 5. Extract Request from Packet Body [cite: 255-256]
    Request rxReq(rxPkt.getData(), rxPkt.getPloadLength());

    // 6. Final Data Validation (REQ-SYS-030) 
    assert(rxReq.getType() == req_weather);
    assert(std::string(rxReq.getBody(), rxReq.getBsize()) == "CYKF_CLEAR_15C");
    
    std::cout << "[PASS] Weather Request reconstructed successfully." << std::endl;
    std::cout << "All Integration Tests Passed!" << std::endl;

    // Proper Cleanup 
    delete[] serializedReq; 
    return 0;
}