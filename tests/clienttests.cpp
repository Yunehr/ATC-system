#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "../client/FileReceiver.hpp"
#include "../shared/packet.h"

class FileReceiverTest : public ::testing::Test {
protected:
    void SetUp() override {
        testPath = "test_manual.pdf";
        error = "";
    }

   void TearDown() override {
    // Manually trigger the receiver to drop the file handle
    std::error_code ec;
    // Attempt removal, but don't let a lock crash the whole suite
    std::filesystem::remove(testPath + ".part", ec); 
    std::filesystem::remove(testPath, ec);
}

    FileReceiver receiver;
    std::string testPath;
    std::string error;
};

// --- REQ-CLT-060: Verification of Successful Transfer Logic ---
TEST_F(FileReceiverTest, FullTransferAndFinalize) {
    ASSERT_TRUE(receiver.start(testPath, error)) << error;

    // Simulate Packet 1: Offset 0, "Hello"
    uint32_t offset1 = 0;
    char payload1[9]; // 4 bytes offset + 5 bytes data
    std::memcpy(payload1, &offset1, 4);
    std::memcpy(payload1 + 4, "Hello", 5);

    packet p1;
    p1.PopulPacket(payload1, 9, 1, pkt_dat);
    p1.setTransmitFlag(PKT_TRNSMT_INCOMP); // More chunks coming

    ASSERT_TRUE(receiver.processPacket(p1, error)) << error;
    EXPECT_FALSE(receiver.isComplete());

    // Simulate Packet 2: Offset 5, "World", FINAL
    uint32_t offset2 = 5;
    char payload2[9];
    std::memcpy(payload2, &offset2, 4);
    std::memcpy(payload2 + 4, "World", 5);

    packet p2;
    p2.PopulPacket(payload2, 9, 1, pkt_dat);
    p2.setTransmitFlag(PKT_TRNSMT_COMP); // Final chunk

    ASSERT_TRUE(receiver.processPacket(p2, error)) << error;
    EXPECT_TRUE(receiver.isComplete());

    // Finalize the file
    ASSERT_TRUE(receiver.finalize(error)) << error;
    EXPECT_TRUE(std::filesystem::exists(testPath));
}

// --- REQ-SVR-040: Error Handling for Incorrect Packet Types ---
TEST_F(FileReceiverTest, RejectsNonDataPackets) {
    receiver.start(testPath, error);

    packet badPkt;
    char msg[] = "WrongType";
    badPkt.PopulPacket(msg, sizeof(msg), 1, pkt_req); // Request type, not Data

    bool result = receiver.processPacket(badPkt, error);
    EXPECT_FALSE(result);
    EXPECT_EQ(error, "Unexpected packet type during manual transfer");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}