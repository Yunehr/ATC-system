/**
 * @file integration_test.cpp
 * @brief Integration QA test for packet serialization and request reconstruction.
 *
 * This test validates the full end-to-end pipeline of creating a weather request,
 * wrapping it in a packet, simulating transport serialization, and reconstructing
 * the original request from the received bytes. CRC integrity and payload accuracy
 * are both verified.
 *
 * Requirements covered:
 * - REQ-SVR-040: Weather request creation
 * - REQ-SYS-020: Wrapping requests in packets
 * - REQ-COM-010: Serialization for transport
 * - REQ-INT-040: Receiving and rebuilding packets
 * - REQ-SYS-030: Final data validation
 */
#include <iostream>
#include <cassert>
#include <string>
#include "packet.h"
#include "request.h"
/**
 * @brief Entry point for the integration QA test suite.
 *
 * Executes a sequential integration test covering the following stages:
 *
 * 1. **Weather Request Creation** (REQ-SVR-040): Constructs a @c Request object
 *    of type @c req_weather with a mock METAR-style payload (@c "CYKF_CLEAR_15C").
 *
 * 2. **Packet Population** (REQ-SYS-020): Serializes the request and wraps it
 *    inside a @c packet using @c PopulPacket(), assigning sequence ID 1 and
 *    type @c pkt_req.
 *
 * 3. **Transport Serialization** (REQ-COM-010): Calls @c Serialize() on the
 *    transmit packet to produce a flat byte buffer suitable for wire transport.
 *
 * 4. **Receive and Rebuild** (REQ-INT-040): Reconstructs a @c packet from the
 *    raw wire buffer and verifies CRC integrity against the original packet.
 *
 * 5. **Payload Extraction**: Extracts the @c Request from the received packet's
 *    body using payload length metadata.
 *
 * 6. **Data Validation** (REQ-SYS-030): Asserts that the reconstructed request
 *    matches the original in both type and body content.
 *
 * @note Heap memory allocated by @c serializeRequest() is explicitly freed
 *       before return. Memory allocated by @c Serialize() is not freed here —
 *       verify ownership semantics in @c packet if this becomes a concern.
 *
 * @return 0 on success. Terminates via @c assert() failure if any check fails.
 */

int main() {
    std::cout << "Starting Integration QA Test..." << std::endl;

    // 1. Create a Weather Request (REQ-SVR-040)
    char weatherData[] = "CYKF_CLEAR_15C"; 
    int reqSize = sizeof(weatherData) - 1; // Correctly ignore null terminator
    Request txReq(req_weather, reqSize, weatherData);

    // 2. Wrap Request in a Packet (REQ-SYS-020) 
    int serializedSize = 0;
    char* serializedReq = txReq.serializeRequest(&serializedSize);
    
    packet txPkt;
    txPkt.PopulPacket(serializedReq, serializedSize, 1, pkt_req);

    // 3. Simulate Serialization for Transport (REQ-COM-010)
    int totalBytes = 0;
    char* wireBuffer = txPkt.Serialize(&totalBytes);

    // 4. Simulate Receiving and Rebuilding (REQ-INT-040)
    packet rxPkt(wireBuffer);
    
    // Verify CRC before processing
    assert(txPkt.calcCRC() == rxPkt.calcCRC());
    std::cout << "[PASS] Integration CRC Checksum verified." << std::endl;

    // 5. Extract Request from Packet Body 
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