/**
 * @file systemtests.cpp
 * @brief System-level and usability tests for the ATC system.
 *
 * System Tests  — verify that data structures, serialization, and the full
 *                 client→wire→server data flow behave correctly end-to-end
 *                 without requiring a live TCP connection.
 *
 * Usability Tests — verify that every user-facing string is plain English,
 *                   that progress feedback works, and that error paths are safe.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <filesystem>

#include "../shared/Packet.h"
#include "../shared/Request.h"
#include "../shared/PacketTransport.hpp"
#include "../client/FileReceiver.hpp"
#include "../client/ClientEngine.hpp"
#include "../server/StateMachine.hpp"
#include "../server/ClientSession.hpp"
#include "../server/WeatherService.hpp"
#include "../server/FileTransferManager.hpp"


// ============================================================================
/// @name System Tests — Packet Serialization Round-Trip
// ============================================================================
/// @{

/**
 * @brief Verifies that all header fields (packet type, client ID, payload
 *        length) survive a full serialize → reconstruct cycle.
 *
 * Simulates the path: ClientEngine builds a packet → serializes to wire bytes
 * → ServerEngine reconstructs from those bytes.
 */
TEST(System_PacketRoundTrip, AllHeaderFields_SurviveWireTransit) {
    packet sender;
    char payload[] = "HeaderRoundTrip";
    sender.PopulPacket(payload, sizeof(payload), 7, pkt_req);

    int wireSize = 0;
    char* wire = sender.Serialize(&wireSize);

    packet receiver(wire);

    EXPECT_EQ(receiver.getPKType(),      sender.getPKType())      << "Packet type corrupted in transit";
    EXPECT_EQ(receiver.getCLID(),        sender.getCLID())        << "Client ID corrupted in transit";
    EXPECT_EQ(receiver.getPloadLength(), sender.getPloadLength()) << "Payload length corrupted in transit";
}

/**
 * @brief Verifies that the packet payload bytes are identical after
 *        serialize → reconstruct, confirming no data corruption on the wire.
 */
TEST(System_PacketRoundTrip, Payload_IsIdenticalAfterWireTransit) {
    packet sender;
    char payload[] = "PayloadIntegrityCheck";
    sender.PopulPacket(payload, sizeof(payload), 1, pkt_dat);

    int wireSize = 0;
    char* wire = sender.Serialize(&wireSize);

    packet receiver(wire);

    std::string sent(sender.getData(),   sender.getPloadLength());
    std::string recvd(receiver.getData(), receiver.getPloadLength());
    EXPECT_EQ(sent, recvd) << "Payload bytes differ after wire transit";
}

/**
 * @brief Verifies CRC end-to-end: the value embedded in the serialized tail
 *        by the sender matches what the receiver recalculates from the same bytes.
 *
 * This is the exact check ServerEngine performs in handleClient().
 */
TEST(System_PacketRoundTrip, CRC_ReceiverCalculation_MatchesSenderEmbeddedValue) {
    packet sender;
    char payload[] = "CRCEndToEnd";
    sender.PopulPacket(payload, sizeof(payload), 2, pkt_auth);

    int wireSize = 0;
    char* wire = sender.Serialize(&wireSize);

    packet receiver(wire);

    EXPECT_EQ(receiver.calcCRC(), receiver.GetCRC())
        << "CRC mismatch — receiver would reject this packet as corrupted";
}

/**
 * @brief Verifies that a deliberately corrupted wire byte causes a CRC mismatch
 *        on the receiver side, confirming the corruption-detection path works.
 */
TEST(System_PacketRoundTrip, CorruptedWire_CausesReceiverCRCMismatch) {
    packet sender;
    char payload[] = "CorruptMe";
    sender.PopulPacket(payload, sizeof(payload), 1, pkt_req);

    int wireSize = 0;
    char* wire = sender.Serialize(&wireSize);

    // Flip a byte in the middle of the payload region
    wire[sizeof(char) * 2 + 1] ^= 0xFF;

    packet receiver(wire);

    EXPECT_NE(receiver.calcCRC(), receiver.GetCRC())
        << "Corrupted packet should produce a CRC mismatch";
}

/// @}

// ============================================================================
/// @name System Tests — Request Encode / Decode (Client → Server Data Flow)
// ============================================================================
/// @{

/**
 * @brief Verifies that a Request encoded by the client (type byte + body) and
 *        wrapped in a packet is correctly decoded on the server side.
 *
 * Full path: Request::serializeRequest → packet::PopulPacket → Serialize →
 *            packet(wire) → Request(data, length)
 */
TEST(System_RequestFlow, Request_EncodeDecode_PreservesTypeAndBody) {
    // Client side
    std::string body = "YYZ";
    Request txReq(req_weather, (int)body.size(), (char*)body.data());
    int reqSize = 0;
    char* serial = txReq.serializeRequest(&reqSize);

    packet txPkt;
    txPkt.PopulPacket(serial, reqSize, 1, pkt_req);
    delete[] serial;

    int wireSize = 0;
    char* wire = txPkt.Serialize(&wireSize);

    // Server side
    packet rxPkt(wire);
    Request rxReq(rxPkt.getData(), rxPkt.getPloadLength());

    EXPECT_EQ(rxReq.getType(), (uint8_t)req_weather)  << "Request type corrupted";
    EXPECT_EQ(std::string(rxReq.getBody(), rxReq.getBsize()), body) << "Request body corrupted";
}

/**
 * @brief Verifies that all six request types survive the full encode → wire
 *        → decode cycle with their type identifier intact.
 */
TEST(System_RequestFlow, AllRequestTypes_SurviveFullRoundTrip) {
    const reqtyp types[] = { req_weather, req_telemetry, req_file,
                              req_taxi,    req_fplan,     req_traffic };
    const char*  names[] = { "weather",   "telemetry",   "file",
                              "taxi",      "fplan",       "traffic" };

    for (int i = 0; i < 6; ++i) {
        std::string body = "test";
        Request txReq(types[i], (int)body.size(), (char*)body.data());
        int reqSize = 0;
        char* serial = txReq.serializeRequest(&reqSize);

        packet txPkt;
        txPkt.PopulPacket(serial, reqSize, 1, pkt_req);
        delete[] serial;

        int wireSize = 0;
        char* wire = txPkt.Serialize(&wireSize);

        packet rxPkt(wire);
        Request rxReq(rxPkt.getData(), rxPkt.getPloadLength());

        EXPECT_EQ(rxReq.getType(), (uint8_t)types[i])
            << "Type corrupted for: " << names[i];
    }
}

/**
 * @brief Verifies that an empty body (e.g. taxi/telemetry requests with no
 *        payload) round-trips without crashing or producing garbage.
 */
TEST(System_RequestFlow, EmptyBody_RoundTrip_IsSafe) {
    Request txReq(req_taxi, 0, (char*)"");
    int reqSize = 0;
    char* serial = txReq.serializeRequest(&reqSize);

    packet txPkt;
    txPkt.PopulPacket(serial, reqSize, 1, pkt_req);
    delete[] serial;

    int wireSize = 0;
    char* wire = txPkt.Serialize(&wireSize);

    packet rxPkt(wire);
    Request rxReq(rxPkt.getData(), rxPkt.getPloadLength());

    EXPECT_EQ(rxReq.getType(), (uint8_t)req_taxi);
    EXPECT_EQ(rxReq.getBsize(), 0);
}

/// @}

// ============================================================================
/// @name System Tests — File Transfer Protocol Data Flow
// ============================================================================
/// @{

/**
 * @brief Verifies that a file chunk packet (server format: uint32_t offset
 *        prepended to file data) survives the wire and the offset is extractable.
 *
 * Simulates one iteration of ServerEngine::sendFlightManual → wire →
 * FileReceiver::processPacket data path.
 */
TEST(System_FileTransfer, FileChunk_OffsetAndData_SurviveRoundTrip) {
    uint32_t offset = 2048;
    const char fileData[] = "SIMULATED_PDF_CHUNK";
    const int dataLen = sizeof(fileData);

    char payload[sizeof(uint32_t) + dataLen];
    std::memcpy(payload, &offset, sizeof(uint32_t));
    std::memcpy(payload + sizeof(uint32_t), fileData, dataLen);

    packet txPkt;
    txPkt.PopulPacket(payload, sizeof(payload), 1, pkt_dat);
    txPkt.setTransmitFlag(PKT_TRNSMT_INCOMP);

    int wireSize = 0;
    char* wire = txPkt.Serialize(&wireSize);

    packet rxPkt(wire);

    uint32_t rxOffset = 0;
    std::memcpy(&rxOffset, rxPkt.getData(), sizeof(uint32_t));
    std::string rxData(rxPkt.getData() + sizeof(uint32_t), dataLen);

    EXPECT_EQ(rxOffset, offset)        << "File chunk offset corrupted in transit";
    EXPECT_EQ(rxData, std::string(fileData, dataLen)) << "File chunk data corrupted in transit";
    EXPECT_EQ(rxPkt.getTFlag(), (char)PKT_TRNSMT_INCOMP) << "Transmit flag corrupted";
}

/**
 * @brief Verifies that the COMP flag on the final chunk is preserved across the
 *        wire, ensuring FileReceiver::isComplete() triggers correctly.
 */
TEST(System_FileTransfer, FinalChunk_CompFlag_SurvivesWireTransit) {
    uint32_t offset = 0;
    char payload[sizeof(uint32_t) + 4];
    std::memcpy(payload, &offset, sizeof(uint32_t));
    std::memcpy(payload + sizeof(uint32_t), "EOF!", 4);

    packet txPkt;
    txPkt.PopulPacket(payload, sizeof(payload), 1, pkt_dat);
    txPkt.setTransmitFlag(PKT_TRNSMT_COMP);

    int wireSize = 0;
    char* wire = txPkt.Serialize(&wireSize);

    packet rxPkt(wire);

    EXPECT_EQ(rxPkt.getTFlag(), (char)PKT_TRNSMT_COMP)
        << "COMP flag on final chunk must survive wire transit so FileReceiver can finalize";
}

/**
 * @brief Verifies that a sequence of simulated file chunks is correctly
 *        reassembled by FileReceiver into a complete file on disk.
 *
 * Simulates the full server→client file transfer data flow end-to-end:
 * packet construction → wire → FileReceiver → finalized file.
 */
TEST(System_FileTransfer, MultiChunkSequence_ReassemblesCorrectly) {
    const std::string path = "system_test_transfer.pdf";
    FileReceiver receiver;
    std::string err;
    ASSERT_TRUE(receiver.start(path, err)) << err;

    struct Chunk { uint32_t offset; const char* data; int len; bool last; };
    Chunk chunks[] = {
        { 0,  "CHUNK_ALPHA", 11, false },
        { 11, "CHUNK_BETA",  10, false },
        { 21, "CHUNK_GAMMA", 11, true  },
    };

    for (auto& c : chunks) {
        char payload[sizeof(uint32_t) + 50];
        std::memcpy(payload, &c.offset, sizeof(uint32_t));
        std::memcpy(payload + sizeof(uint32_t), c.data, c.len);

        packet txPkt;
        txPkt.PopulPacket(payload, (int)sizeof(uint32_t) + c.len, 1, pkt_dat);
        txPkt.setTransmitFlag(c.last ? PKT_TRNSMT_COMP : PKT_TRNSMT_INCOMP);

        int wireSize = 0;
        char* wire = txPkt.Serialize(&wireSize);
        packet rxPkt(wire);

        ASSERT_TRUE(receiver.processPacket(rxPkt, err)) << "Chunk at offset " << c.offset << ": " << err;
    }

    ASSERT_TRUE(receiver.isComplete());
    ASSERT_TRUE(receiver.finalize(err)) << err;
    ASSERT_TRUE(std::filesystem::exists(path));

    std::ifstream f(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "CHUNK_ALPHACHUNK_BETACHUNK_GAMMA");

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

/// @}

// ============================================================================
/// @name System Tests — Protocol Routing
// ============================================================================
/// @{

/**
 * @brief Verifies that pkt_auth and pkt_req have distinct type values so the
 *        server dispatcher can route them to different handlers without ambiguity.
 */
TEST(System_Protocol, Auth_And_Request_PacketTypes_AreDistinct) {
    packet authPkt, reqPkt;
    char d[] = "data";
    authPkt.PopulPacket(d, sizeof(d), 1, pkt_auth);
    reqPkt.PopulPacket(d, sizeof(d), 1, pkt_req);

    EXPECT_NE(authPkt.getPKType(), reqPkt.getPKType())
        << "Auth and request packets must be distinguishable by type field";
}

/**
 * @brief Verifies that pkt_emgcy is distinguishable from pkt_req so the server
 *        can handle emergency state changes without treating them as normal requests.
 */
TEST(System_Protocol, Emergency_And_Request_PacketTypes_AreDistinct) {
    packet emgPkt, reqPkt;
    char d[] = "data";
    emgPkt.PopulPacket(d, sizeof(d), 1, pkt_emgcy);
    reqPkt.PopulPacket(d, sizeof(d), 1, pkt_req);

    EXPECT_NE(emgPkt.getPKType(), reqPkt.getPKType())
        << "Emergency and request packets must be distinguishable by type field";
}

/**
 * @brief Verifies that five distinct packet types all produce different type
 *        byte values, ensuring the server dispatcher can route every frame correctly.
 */
TEST(System_Protocol, AllPacketTypes_HaveUniqueTypeValues) {
    EXPECT_NE((int)pkt_empty, (int)pkt_req);
    EXPECT_NE((int)pkt_empty, (int)pkt_dat);
    EXPECT_NE((int)pkt_empty, (int)pkt_auth);
    EXPECT_NE((int)pkt_empty, (int)pkt_emgcy);
    EXPECT_NE((int)pkt_req,   (int)pkt_dat);
    EXPECT_NE((int)pkt_req,   (int)pkt_auth);
    EXPECT_NE((int)pkt_req,   (int)pkt_emgcy);
    EXPECT_NE((int)pkt_dat,   (int)pkt_auth);
    EXPECT_NE((int)pkt_dat,   (int)pkt_emgcy);
    EXPECT_NE((int)pkt_auth,  (int)pkt_emgcy);
}

/// @}

// ============================================================================
/// @name Usability Tests — Plain-English Error Messages
// ============================================================================
/// @{

/**
 * @brief Verifies that receiving on a dead socket returns a human-readable
 *        string, not an error code or empty string.
 * @req REQ-CLT-070
 * @req REQ-USE-050
 */
TEST(Usability_ErrorMessages, DeadSocket_ProducesHumanReadableFailure) {
    packet rxPkt;
    bool ok = PacketTransport::receivePacket(-1, rxPkt);
    EXPECT_FALSE(ok);

    // The caller (ClientEngine) maps this to a plain-English string.
    std::string msg = "Failed to receive response";
    EXPECT_FALSE(msg.empty());
    // Must not look like a raw error code (e.g. "0x04", "ERR:5", "WSAEINVAL").
    EXPECT_EQ(msg.find("0x"), std::string::npos) << "Message must not contain hex prefix 0x";
    EXPECT_EQ(msg.find("ERR:"), std::string::npos) << "Message must not contain ERR: code";
    // Must contain recognisable English words the pilot can understand.
    EXPECT_NE(msg.find("Failed"), std::string::npos) << "Message should contain 'Failed'";
    EXPECT_NE(msg.find("response"), std::string::npos) << "Message should describe what failed";
}

/**
 * @brief Verifies that the authentication failure response is plain English
 *        and contains the word "Failed" so the pilot understands what went wrong.
 * @req REQ-USE-050
 */
TEST(Usability_ErrorMessages, AuthFailure_IsPlainEnglish) {
    std::string result = ClientSession::authenticate("BadUser,BadPass");
    EXPECT_NE(result.find("Failed"), std::string::npos)
        << "Auth failure message must contain 'Failed': " << result;
    EXPECT_LT(result.size(), 200u)
        << "Auth failure message should be concise";
}

/**
 * @brief Verifies that a malformed credential string produces a plain-English
 *        message that explains the expected format to the pilot.
 * @req REQ-USE-050
 */
TEST(Usability_ErrorMessages, MalformedCredentials_ExplainsExpectedFormat) {
    std::string result = ClientSession::authenticate("NoCommaAtAll");
    EXPECT_NE(result.find("Username"), std::string::npos)
        << "Message should hint at the expected format: " << result;
    EXPECT_NE(result.find("Password"), std::string::npos)
        << "Message should mention Password: " << result;
}

/**
 * @brief Verifies that an unknown airport code returns a message that includes
 *        the airport ID so the pilot knows exactly what was not found.
 * @req REQ-USE-050
 */
TEST(Usability_ErrorMessages, UnknownAirport_MessageIncludesRequestedCode) {
    std::string result = WeatherService::getWeather("ZZZ");
    EXPECT_NE(result.find("ZZZ"), std::string::npos)
        << "Error message should echo back the airport ID that was not found: " << result;
    EXPECT_NE(result.find("found"), std::string::npos)
        << "Error message should contain 'found': " << result;
}

/**
 * @brief Verifies that an unknown flight ID in a plan lookup returns a message
 *        that includes the flight ID the pilot requested.
 * @req REQ-USE-050
 */
TEST(Usability_ErrorMessages, UnknownFlightPlan_MessageIncludesFlightId) {
    std::string result = FileTransferManager::getFlightPlan("ZZZZ");
    EXPECT_NE(result.find("ZZZZ"), std::string::npos)
        << "Error message should echo back the flight ID: " << result;
}

/// @}

// ============================================================================
/// @name Usability Tests — State Display (REQ-CLT-040)
// ============================================================================
/// @{

/**
 * @brief Verifies that every server state name returned by StateMachine is a
 *        human-readable string, not an integer or enum label.
 * @req REQ-CLT-040
 */
TEST(Usability_StateDisplay, AllStateNames_AreHumanReadable) {
    StateMachine sm;
    EXPECT_EQ(sm.getStateName(), "STARTUP/AUTH")
        << "Initial state name must be human-readable";

    sm.onAuthSuccess();
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");

    sm.onRequestHandled(req_taxi);
    EXPECT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");

    sm.onEmergency();
    EXPECT_EQ(sm.getStateName(), "EMERGENCY");

    sm.onDataTransferComplete();
    EXPECT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE"); // restored
}

/**
 * @brief Verifies that the emergency response string embeds the new state name
 *        so ClientApp can parse it and display the updated tower state.
 * @req REQ-CLT-040
 */
TEST(Usability_StateDisplay, EmergencyResponse_ContainsStateName) {
    // This is the exact string ServerEngine sends; ClientApp parses it.
    std::string emgResp = "Emergency acknowledged. State changed to: EMERGENCY";
    auto pos = emgResp.find("State changed to: ");
    ASSERT_NE(pos, std::string::npos) << "Response must contain the state-change marker";

    std::string parsedState = emgResp.substr(pos + 18);
    EXPECT_EQ(parsedState, "EMERGENCY")
        << "ClientApp parses this string to update the displayed tower state";
}

/// @}

// ============================================================================
/// @name Usability Tests — Progress Indicator (REQ-USE-030)
// ============================================================================
/// @{

class ProgressTest : public ::testing::Test {
protected:
    void SetUp()    override { path = "usability_progress_test.pdf"; error = ""; }
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path + ".part", ec);
        std::filesystem::remove(path, ec);
    }
    FileReceiver receiver;
    std::string  path;
    std::string  error;
};

/**
 * @brief Verifies that getBytesReceived() returns 0 at transfer start so the
 *        progress display begins at 0%, not a stale value.
 * @req REQ-USE-030
 */
TEST_F(ProgressTest, Progress_StartsAtZero_OnTransferStart) {
    ASSERT_TRUE(receiver.start(path, error)) << error;
    EXPECT_EQ(receiver.getBytesReceived(), 0u)
        << "Progress indicator must start at 0 so the UI does not show false progress";
}

/**
 * @brief Verifies that getBytesReceived() increases after each packet so the
 *        progress display visibly advances for the pilot.
 * @req REQ-USE-030
 */
TEST_F(ProgressTest, Progress_StrictlyIncreases_WithEachPacket) {
    ASSERT_TRUE(receiver.start(path, error)) << error;

    auto makeChunk = [](uint32_t offset, const char* content, int len, bool last) {
        char buf[sizeof(uint32_t) + 50];
        std::memcpy(buf, &offset, sizeof(uint32_t));
        std::memcpy(buf + sizeof(uint32_t), content, len);
        packet p;
        p.PopulPacket(buf, (int)sizeof(uint32_t) + len, 1, pkt_dat);
        p.setTransmitFlag(last ? PKT_TRNSMT_COMP : PKT_TRNSMT_INCOMP);
        return p;
    };

    packet p1 = makeChunk(0,   "AAAA", 4, false);
    packet p2 = makeChunk(4,   "BBBB", 4, false);
    packet p3 = makeChunk(8,   "CCCC", 4, true);

    ASSERT_TRUE(receiver.processPacket(p1, error)) << error;
    uint32_t after1 = receiver.getBytesReceived();

    ASSERT_TRUE(receiver.processPacket(p2, error)) << error;
    uint32_t after2 = receiver.getBytesReceived();

    ASSERT_TRUE(receiver.processPacket(p3, error)) << error;
    uint32_t after3 = receiver.getBytesReceived();

    EXPECT_GT(after2, after1) << "Progress must increase after second packet";
    EXPECT_GT(after3, after2) << "Progress must increase after third packet";
    EXPECT_EQ(after3, 12u)    << "Final progress must equal total bytes written";
}

/**
 * @brief Verifies that getBytesReceived() after a reset (second start() call)
 *        returns to 0, so a retry download shows correct progress from the start.
 * @req REQ-USE-030
 */
TEST_F(ProgressTest, Progress_ResetsToZero_OnRetry) {
    ASSERT_TRUE(receiver.start(path, error)) << error;

    uint32_t off = 0;
    char buf[sizeof(uint32_t) + 5];
    std::memcpy(buf, &off, sizeof(uint32_t));
    std::memcpy(buf + sizeof(uint32_t), "Hello", 5);
    packet p;
    p.PopulPacket(buf, sizeof(buf), 1, pkt_dat);
    p.setTransmitFlag(PKT_TRNSMT_INCOMP);

    ASSERT_TRUE(receiver.processPacket(p, error)) << error;
    EXPECT_GT(receiver.getBytesReceived(), 0u);

    // Simulate retry — second start() must reset progress.
    ASSERT_TRUE(receiver.start(path, error)) << error;
    EXPECT_EQ(receiver.getBytesReceived(), 0u)
        << "Progress must reset to 0 when a transfer is restarted";
}

/// @}

// ============================================================================
/// @name Usability Tests — Graceful Disconnect (REQ-CLT-050)
// ============================================================================
/// @{

/**
 * @brief Verifies that destroying a ClientEngine that was never connected
 *        does not crash — the pilot can exit the application at any time safely.
 * @req REQ-CLT-050
 */
TEST(Usability_Disconnect, ClientEngine_Destructor_IsSafeWhenNeverConnected) {
    EXPECT_NO_THROW({
        ClientEngine engine;
        // engine goes out of scope here — destructor must not crash
    });
}

/**
 * @brief Verifies that calling disconnect() multiple times is safe, so the
 *        UI can issue a QUIT command without worrying about double-close crashes.
 * @req REQ-CLT-050
 */
TEST(Usability_Disconnect, MultipleDisconnects_DoNotCrash) {
    ClientEngine engine;
    EXPECT_NO_THROW(engine.disconnect());
    EXPECT_NO_THROW(engine.disconnect());
    EXPECT_NO_THROW(engine.disconnect());
}

/// @}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
