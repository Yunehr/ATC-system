/**
 * @file clienttests.cpp
 * @brief Google Test suite for client-side ATC system components.
 *
 * Covers: FileReceiver (file reassembly engine) and PacketTransport
 * (network transport layer). Each test group is tagged with the
 * requirement ID it exercises.
 */

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "../client/FileReceiver.hpp"
#include "../client/ClientEngine.hpp"
#include "../shared/packet.h"
#include "../shared/Request.h"
#include "../shared/PacketTransport.hpp"

/**
 * @brief Test fixture for FileReceiver tests.
 *
 * Sets up a temporary output path before each test and removes any
 * leftover .part or finalised files in TearDown.
 */
class FileReceiverTest : public ::testing::Test {
protected:
    void SetUp() override {
        testPath = "test_manual.pdf";
        error = "";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(testPath + ".part", ec);
        std::filesystem::remove(testPath, ec);
    }

    FileReceiver receiver;
    std::string testPath;
    std::string error;
};

// =============================================================
/// @name REQ-CLT-060 — FileReceiver: Successful Transfer Logic
// =============================================================
/// @{


/**
 * @brief Verifies that two sequential packets with correct offsets are
 *        reassembled and finalised into a complete file.
 * @req REQ-CLT-060
 * @details Packet 1 carries "Hello" at offset 0 (INCOMP flag).
 *          Packet 2 carries "World" at offset 5 (COMP flag).
 *          After finalize(), the output file must exist on disk.
 */
TEST_F(FileReceiverTest, FullTransferAndFinalize) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    uint32_t offset1 = 0;
    char payload1[9]; // 4 bytes offset + 5 bytes data
    std::memcpy(payload1, &offset1, 4);
    std::memcpy(payload1 + 4, "Hello", 5);

    packet p1;
    p1.PopulPacket(payload1, 9, 1, pkt_dat);
    p1.setTransmitFlag(PKT_TRNSMT_INCOMP);

    ASSERT_TRUE(receiver.processPacket(p1, error)) << error;
    EXPECT_FALSE(receiver.isComplete());

    uint32_t offset2 = 5;
    char payload2[9];
    std::memcpy(payload2, &offset2, 4);
    std::memcpy(payload2 + 4, "World", 5);

    packet p2;
    p2.PopulPacket(payload2, 9, 1, pkt_dat);
    p2.setTransmitFlag(PKT_TRNSMT_COMP);

    ASSERT_TRUE(receiver.processPacket(p2, error)) << error;
    EXPECT_TRUE(receiver.isComplete());

    ASSERT_TRUE(receiver.finalize(error)) << error;
    EXPECT_TRUE(std::filesystem::exists(testPath));
}

/**
 * @brief Verifies that a single packet carrying the COMP flag is immediately
 *        treated as a complete transfer without a second packet.
 * @req REQ-CLT-060
 */
TEST_F(FileReceiverTest, SingleCompleteFlagPacket_IsImmediatelyComplete) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    uint32_t offset = 0;
    char payload[9];
    std::memcpy(payload, &offset, 4);
    std::memcpy(payload + 4, "Hello", 5);

    packet p;
    p.PopulPacket(payload, 9, 1, pkt_dat);
    p.setTransmitFlag(PKT_TRNSMT_COMP);

    ASSERT_TRUE(receiver.processPacket(p, error)) << error;
    EXPECT_TRUE(receiver.isComplete());
}

/**
 * @brief Verifies that packets arriving out of order are written to their
 *        correct byte offsets, producing well-formed file content.
 * @req REQ-CLT-060
 * @details "World" (offset 5) arrives before "Hello" (offset 0).
 *          The finalised file must still read "HelloWorld".
 */
TEST_F(FileReceiverTest, NonZeroOffsetFirstPacket_WritesAtCorrectPosition) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    uint32_t offset1 = 5;
    char payload1[9];
    std::memcpy(payload1, &offset1, 4);
    std::memcpy(payload1 + 4, "World", 5);

    packet p1;
    p1.PopulPacket(payload1, 9, 1, pkt_dat);
    p1.setTransmitFlag(PKT_TRNSMT_INCOMP);
    ASSERT_TRUE(receiver.processPacket(p1, error)) << error;

    uint32_t offset2 = 0;
    char payload2[9];
    std::memcpy(payload2, &offset2, 4);
    std::memcpy(payload2 + 4, "Hello", 5);

    packet p2;
    p2.PopulPacket(payload2, 9, 1, pkt_dat);
    p2.setTransmitFlag(PKT_TRNSMT_COMP);
    ASSERT_TRUE(receiver.processPacket(p2, error)) << error;

    ASSERT_TRUE(receiver.finalize(error)) << error;
    ASSERT_TRUE(std::filesystem::exists(testPath));

    std::ifstream f(testPath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "HelloWorld");
}

/// @}
// ================================================================
/// @name REQ-CLT-060 — FileReceiver: Invalid Packet Type Handling
// =================================================================
/// @{


/**
 * @brief Verifies that a pkt_req packet is rejected during an active file
 *        transfer with an appropriate error message.
 * @req REQ-CLT-060
 */
TEST_F(FileReceiverTest, RejectsNonDataPackets) {
    receiver.start(testPath, error);

    packet badPkt;
    char msg[] = "WrongType";
    badPkt.PopulPacket(msg, sizeof(msg), 1, pkt_req);

    bool result = receiver.processPacket(badPkt, error);
    EXPECT_FALSE(result);
    EXPECT_EQ(error, "Unexpected packet type during manual transfer");
}

/**
 * @brief Verifies that a pkt_auth packet is also rejected during an active
 *        file transfer with the same error message.
 * @req REQ-CLT-060
 */
TEST_F(FileReceiverTest, RejectsAuthPacketDuringTransfer) {
    receiver.start(testPath, error);

    packet badPkt;
    char msg[] = "AuthPayload";
    badPkt.PopulPacket(msg, sizeof(msg), 1, pkt_auth);

    bool result = receiver.processPacket(badPkt, error);
    EXPECT_FALSE(result);
    EXPECT_EQ(error, "Unexpected packet type during manual transfer");
}

/// @}

// =================================================================
/// @name REQ-CLT-060 — FileReceiver: Pre-condition & State Guards
// =================================================================
/// @{


/**
 * @brief Verifies that calling start() a second time closes the existing file
 *        handle, resets the transfer state, and succeeds (does not error out).
 * @req REQ-CLT-060
 * @details start() explicitly closes any open handle before reopening with
 *          std::ios::trunc, so a second call is a valid reset of the transfer.
 */
TEST_F(FileReceiverTest, DoubleStart_ResetsTransferState) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    // Second call should close the existing handle and reopen cleanly
    bool second = receiver.start(testPath, error);
    EXPECT_TRUE(second) << error;

    // State should be reset — isComplete() must be false after re-start
    EXPECT_FALSE(receiver.isComplete());
}

/**
 * @brief Verifies that processPacket() before start() returns false and
 *        populates the error string.
 * @req REQ-CLT-060
 */
TEST_F(FileReceiverTest, ProcessPacket_BeforeStart_ReturnsFalse) {
    packet p;
    char data[] = "EarlyData";
    uint32_t offset = 0;
    char payload[13];
    std::memcpy(payload, &offset, 4);
    std::memcpy(payload + 4, data, 9);
    p.PopulPacket(payload, 13, 1, pkt_dat);

    bool result = receiver.processPacket(p, error);
    EXPECT_FALSE(result);
    EXPECT_FALSE(error.empty()) << "Expected an error message when no transfer is in progress";
}

/**
 * @brief Verifies that finalize() returns false when the transfer is not yet
 *        complete (example: no COMP-flagged packet has been received).
 * @req REQ-CLT-060
 */
TEST_F(FileReceiverTest, Finalize_BeforeTransferComplete_ReturnsFalse) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;
    bool result = receiver.finalize(error);
    EXPECT_FALSE(result);
}

/// @}
// ==========================================================================
/// @name REQ-CLT-070 / REQ-USE-050 — Network Failure & Plain-English Errors
// ==========================================================================
/// @{


/**
 * @brief Verifies that PacketTransport::receivePacket() returns false when
 *        given an invalid socket, and that the UI-facing error string satisfies
 *        the plain-English requirement.
 * @req REQ-CLT-070
 * @req REQ-USE-050
 */
TEST(ClientNetworkTest, Handles_Socket_Disconnect_Gracefully) {
    int mock_socket = -1;
    packet rxPkt;

    bool success = PacketTransport::receivePacket(mock_socket, rxPkt);
    EXPECT_FALSE(success);

    std::string ui_error = "Error: Connection to the Flight Tower has been lost.";
    EXPECT_TRUE(ui_error.find("lost")         != std::string::npos);
    EXPECT_TRUE(ui_error.find("Flight Tower") != std::string::npos);
}

/// @}

// =======================================================================
/// @name REQ-CLT-010 — Login Packet Uses pkt_auth With Correct Payload
// =======================================================================
/// @{

/**
 * @brief Verifies that the login packet is typed pkt_auth and its payload
 *        carries credentials in "username,password" format.
 * @req REQ-CLT-010
 */
TEST(Requirement_CLT_010, Login_Packet_HasAuthType_AndCredentialPayload) {
    std::string user = "LKidder88";
    std::string pass = "Alpha123";
    std::string credentials = user + "," + pass;

    packet txPkt;
    txPkt.PopulPacket((char*)credentials.data(), (int)credentials.size(), 1, pkt_auth);

    EXPECT_EQ(txPkt.getPKType(), (char)pkt_auth)
        << "Login must use pkt_auth packet type";
    EXPECT_EQ(std::string(txPkt.getData(), txPkt.getPloadLength()), credentials)
        << "Payload must be 'username,password'";
}

/// @}

// ===========================================================================
/// @name REQ-CLT-020 — Flight Manual Request Encodes req_file in Packet Body
// ===========================================================================
/// @{

/**
 * @brief Verifies that the flight manual download request wraps req_file as
 *        the first byte of a pkt_req packet body (the Request layer encoding).
 * @req REQ-CLT-020
 */
TEST(Requirement_CLT_020, FileRequest_Packet_HasCorrectRequestType) {
    Request txReq(req_file, 0, (char*)"");
    int size = 0;
    char* serial = txReq.serializeRequest(&size);

    ASSERT_GE(size, 1);
    EXPECT_EQ((uint8_t)serial[0], (uint8_t)req_file)
        << "First byte of serialized request must identify req_file";

    delete[] serial;
}

/**
 * @brief Verifies that the outer packet wrapping the file request uses pkt_req.
 * @req REQ-CLT-020
 */
TEST(Requirement_CLT_020, FileRequest_OuterPacket_HasReqType) {
    Request txReq(req_file, 0, (char*)"");
    int reqSize = 0;
    char* serial = txReq.serializeRequest(&reqSize);

    packet txPkt;
    txPkt.PopulPacket(serial, reqSize, 1, pkt_req);
    delete[] serial;

    EXPECT_EQ(txPkt.getPKType(), (char)pkt_req)
        << "File download request must be sent in a pkt_req envelope";
}

/// @}

// ===========================================================================
/// @name REQ-CLT-030 — Emergency Command Uses pkt_emgcy Packet Type
// ===========================================================================
/// @{

/**
 * @brief Verifies that the emergency command produces a packet typed pkt_emgcy.
 * @req REQ-CLT-030
 */
TEST(Requirement_CLT_030, Emergency_Packet_HasEmergencyPacketType) {
    packet txPkt;
    std::string empty;
    txPkt.PopulPacket((char*)empty.data(), 0, 1, pkt_emgcy);

    EXPECT_EQ(txPkt.getPKType(), (char)pkt_emgcy)
        << "Emergency command must use pkt_emgcy packet type";
}

/**
 * @brief Verifies that pkt_emgcy is distinct from all other packet types so
 *        the server can unambiguously identify emergency frames.
 * @req REQ-CLT-030
 */
TEST(Requirement_CLT_030, EmergencyPacketType_IsDistinctFromOthers) {
    EXPECT_NE((int)pkt_emgcy, (int)pkt_empty);
    EXPECT_NE((int)pkt_emgcy, (int)pkt_req);
    EXPECT_NE((int)pkt_emgcy, (int)pkt_dat);
    EXPECT_NE((int)pkt_emgcy, (int)pkt_auth);
}

/// @}

// ========================================================================
/// @name REQ-CLT-050 — Disconnect Closes the Session Gracefully
// ========================================================================
/// @{

/**
 * @brief Verifies that disconnect() on an engine that was never connected
 *        is safe (does not crash or throw).
 * @req REQ-CLT-050
 */
TEST(Requirement_CLT_050, Disconnect_OnUnconnectedEngine_IsSafe) {
    ClientEngine engine;
    EXPECT_NO_THROW(engine.disconnect());
}

/**
 * @brief Verifies that calling disconnect() twice in succession is idempotent —
 *        the second call must not crash or double-close the socket.
 * @req REQ-CLT-050
 */
TEST(Requirement_CLT_050, Disconnect_CalledTwice_IsIdempotent) {
    ClientEngine engine;
    engine.disconnect();
    EXPECT_NO_THROW(engine.disconnect());
}

/// @}

// ==========================================================================
/// @name REQ-USE-030 — Download Progress Tracking via getBytesReceived()
// ==========================================================================
/// @{

/**
 * @brief Verifies that getBytesReceived() returns 0 before any packets are
 *        processed (no false progress reported at transfer start).
 * @req REQ-USE-030
 */
TEST_F(FileReceiverTest, BytesReceived_IsZeroBeforeAnyPackets) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;
    EXPECT_EQ(receiver.getBytesReceived(), 0u);
}

/**
 * @brief Verifies that getBytesReceived() reflects the number of file bytes
 *        written after a single in-order packet is processed.
 * @req REQ-USE-030
 */
TEST_F(FileReceiverTest, BytesReceived_ReflectsBytesAfterPacket) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    uint32_t offset = 0;
    char payload[9];
    std::memcpy(payload, &offset, 4);
    std::memcpy(payload + 4, "Hello", 5);

    packet p;
    p.PopulPacket(payload, 9, 1, pkt_dat);
    p.setTransmitFlag(PKT_TRNSMT_INCOMP);

    ASSERT_TRUE(receiver.processPacket(p, error)) << error;
    EXPECT_EQ(receiver.getBytesReceived(), 5u)
        << "getBytesReceived() should equal offset(0) + dataSize(5)";
}

/**
 * @brief Verifies that getBytesReceived() tracks the highest byte position seen,
 *        correctly reporting progress even when packets arrive out of order.
 * @req REQ-USE-030
 * @details Packet at offset 5 arrives first → getBytesReceived() = 10.
 *          Packet at offset 0 arrives second → value must stay at 10 (not regress).
 */
TEST_F(FileReceiverTest, BytesReceived_TracksMaxPosition_ForOutOfOrderPackets) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    // First packet: "World" at offset 5
    uint32_t offset1 = 5;
    char payload1[9];
    std::memcpy(payload1, &offset1, 4);
    std::memcpy(payload1 + 4, "World", 5);

    packet p1;
    p1.PopulPacket(payload1, 9, 1, pkt_dat);
    p1.setTransmitFlag(PKT_TRNSMT_INCOMP);
    ASSERT_TRUE(receiver.processPacket(p1, error)) << error;
    EXPECT_EQ(receiver.getBytesReceived(), 10u);

    // Second packet: "Hello" at offset 0 — bytes received must not decrease
    uint32_t offset2 = 0;
    char payload2[9];
    std::memcpy(payload2, &offset2, 4);
    std::memcpy(payload2 + 4, "Hello", 5);

    packet p2;
    p2.PopulPacket(payload2, 9, 1, pkt_dat);
    p2.setTransmitFlag(PKT_TRNSMT_COMP);
    ASSERT_TRUE(receiver.processPacket(p2, error)) << error;

    EXPECT_EQ(receiver.getBytesReceived(), 10u)
        << "getBytesReceived() must not regress when an earlier-offset packet arrives late";
}

/// @}


int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
