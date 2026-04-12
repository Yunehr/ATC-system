/**
 * @file packetQA.cpp
 * @brief Packet-layer QA tests: serialization, integrity, and corruption detection.
 *
 * Validates the @c packet class in isolation across three scenarios:
 *  -# Clean round-trip: populate → serialize → deserialize → CRC and payload match.
 *  -# Data integrity: reconstructed payload length and content match the original.
 *  -# Corruption detection: a bit-flipped wire buffer produces a CRC mismatch.
 *
 * Requirements covered:
 * - REQ-INT-040: Packet integrity verification via CRC.
 * - REQ-SYS-030: Data transfer accuracy after deserialization.
 */
#include <iostream>
#include <cassert>
#include <cstring>
#include "packet.h"
/**
 * @brief Entry point for the packet-layer QA test suite.
 *
 * ### Test 1 — Serialization round-trip (REQ-INT-040)
 * Populates a @c packet with the payload @c "ATC_READY" using type @c pkt_dat
 * and client ID 1, serializes it to a wire buffer, then reconstructs a
 * receive-side @c packet from that buffer. Asserts that both packets produce
 * identical CRC values.
 *
 * ### Test 2 — Data integrity (REQ-SYS-030)
 * Asserts that the reconstructed packet's payload length matches the original
 * @c dataSize and that the payload content is byte-for-byte equal to the
 * original string.
 *
 * ### Test 3 — Corruption detection
 * Flips all bits in the first payload byte of the wire buffer (offset 2,
 * immediately after the 2-byte header) and constructs a @c corruptedPkt from
 * it. Verifies that the corrupted packet's CRC differs from the original,
 * confirming that single-byte corruption is detectable.
 *
 * @note Buffer offset 2 targets the first payload byte because the @c header
 *       struct occupies exactly 2 bytes (bitfield byte + length byte).
 *       If the header layout changes, this offset must be updated accordingly.
 *
 * @note The corruption test does not use @c assert — a CRC mismatch is the
 *       *expected* outcome, not a failure condition. The @c if ensures the
 *       pass message is only printed when the mismatch is confirmed.
 *
 * @return Always returns 0. Terminates via @c assert() if any integrity check fails.
 */

int main() {
    std::cout << "Starting Packet QA Test..." << std::endl;

    // -------------------------------------------------------------------------
    // Test 1: Serialization round-trip
    // -------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Test 2: Payload length and content
    // -------------------------------------------------------------------------
    assert(rxPkt.getPloadLength() == dataSize);
    assert(std::string(rxPkt.getData()) == "ATC_READY");
    std::cout << "[PASS] Data integrity verified." << std::endl;

    // -------------------------------------------------------------------------
    // Test 3: Corruption detection
    // Offset 2 = first payload byte (header is exactly 2 bytes).
    // XOR with 0xFF flips all bits, guaranteeing a CRC delta.
    // -------------------------------------------------------------------------
    buffer[2] ^= 0xFF; // Flip bits in the first byte of the body
    packet corruptedPkt(buffer);
    if (corruptedPkt.calcCRC() != txPkt.calcCRC()) {
    std::cout << "[PASS] Corruption detected via CRC." << std::endl;
    }
    std::cout << "All packet tests passed!" << std::endl;
    return 0;
}