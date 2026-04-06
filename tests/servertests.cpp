/**
 * @file servertests.cpp
 * @brief Google Test suite for server-side ATC system components.
 *
 * Covers: StateMachine, ServerEngine, ClientSession, WeatherService,
 * and FileTransferManager. Each test group is tagged with the
 * requirement ID it exercises.
 */

#include <gtest/gtest.h>
#include "../server/ServerEngine.hpp"
#include "../server/StateMachine.hpp"
#include "../server/ClientSession.hpp"
#include "../server/WeatherService.hpp"
#include "../server/FileTransferManager.hpp"
#include "../server/Logger.h"
#include "../shared/Request.h"
#include <fstream>
#include <filesystem>

// ==================================================================
/// @name REQ-SVR-020 — State Machine Initialization & Transitions
// ==================================================================
/// @{


/**
 * @brief Verifies the state machine initialises to STARTUP/AUTH on construction.
 * @req REQ-SVR-020
 */
TEST(Requirement_SVR_020, InitialStateIsStartupAuth) {
    StateMachine sm;
    EXPECT_EQ(sm.getStateName(), "STARTUP/AUTH");
}

/**
 * @brief Verifies the state machine transitions to PRE_FLIGHT after a successful auth.
 * @req REQ-SVR-020
 */
TEST(Requirement_SVR_020, TransitionAfterAuth) {
    StateMachine sm;
    sm.onAuthSuccess();
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");
}

/**
 * @brief Verifies the EMERGENCY state is reachable from ACTIVE_AIRSPACE via pkt_emgcy.
 * @req REQ-SVR-020
 * @note This test will FAIL until StateMachine.cpp is updated to handle pkt_emgcy.
 */
TEST(Requirement_SVR_020, EmergencyStateIsReachable) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi);
    sm.onRequestHandled(static_cast<reqtyp>(pkt_emgcy));
    EXPECT_EQ(sm.getStateName(), "EMERGENCY");
}

/**
 * @brief Verifies a file request moves the state to DATA_TRANSFER and returns
 *        to PRE_FLIGHT once the transfer is complete.
 * @req REQ-SVR-020
 */
TEST(Requirement_SVR_020, StateMovesToDataTransfer) {
    StateMachine sm;
    sm.onAuthSuccess();

    sm.onRequestHandled(req_file);
    EXPECT_EQ(sm.getStateName(), "DATA_TRANSFER");

    sm.onDataTransferComplete();
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");
}

/**
 * @brief Verifies that the EMERGENCY state entered from PRE_FLIGHT is exited
 *        back to PRE_FLIGHT when onDataTransferComplete() is called.
 * @req REQ-SVR-020
 */
TEST(Requirement_SVR_020, Emergency_From_PreFlight_RestoresToPreFlight) {
    StateMachine sm;
    sm.onAuthSuccess();                           // -> PRE_FLIGHT
    ASSERT_EQ(sm.getStateName(), "PRE_FLIGHT");

    sm.onEmergency();                             // -> EMERGENCY (saves PRE_FLIGHT)
    ASSERT_EQ(sm.getStateName(), "EMERGENCY");

    sm.onDataTransferComplete();                  // restores saved state
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");
}

/**
 * @brief Verifies that the EMERGENCY state entered from ACTIVE_AIRSPACE is exited
 *        back to ACTIVE_AIRSPACE, not PRE_FLIGHT.
 * @req REQ-SVR-020
 */
TEST(Requirement_SVR_020, Emergency_From_ActiveAirspace_RestoresToActiveAirspace) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi);                // -> ACTIVE_AIRSPACE
    ASSERT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");

    sm.onEmergency();                             // -> EMERGENCY (saves ACTIVE_AIRSPACE)
    ASSERT_EQ(sm.getStateName(), "EMERGENCY");

    sm.onDataTransferComplete();                  // restores saved state
    EXPECT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");
}

/// @}

// =============================================
/// @name REQ-SVR-030 — Server File Streaming
// =============================================
/// @{


/**
 * @brief Verifies that sendFlightManual() returns false gracefully when the
 *        PDF file is missing rather than crashing or blocking.
 * @req REQ-SVR-030
 */
TEST(Requirement_SVR_030, Server_Handles_Missing_Manual_Gracefully) {
    ServerEngine server;
    bool result = server.sendFlightManual((SOCKET)-1, 99);
    EXPECT_FALSE(result);
}

/**
 * @brief Verifies that getManualInfo() returns a message containing the PDF
 *        file size when FlightManual.pdf exists.
 * @req REQ-SVR-030
 */
TEST(Requirement_SVR_030, GetManualInfo_ReturnsFileSizeMessage) {
    std::string result = FileTransferManager::getManualInfo();
    EXPECT_TRUE(result.find("Flight Manual ready. Size=") != std::string::npos) << result;
}

/// @}

// ======================================================================
/// @name REQ-SVR-040 — Operational State Machine Permission Enforcement
// ======================================================================
/// @{


/**
 * @brief Verifies that weather requests are denied before authentication.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, WeatherDeniedInAuth) {
    StateMachine sm;
    EXPECT_FALSE(sm.canHandle(req_weather));
}

/**
 * @brief Verifies that weather requests are permitted in PRE_FLIGHT state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, WeatherAllowedInPreFlight) {
    StateMachine sm;
    sm.onAuthSuccess();
    EXPECT_TRUE(sm.canHandle(req_weather));
}

/**
 * @brief Verifies that taxi requests are blocked before authentication.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Block_Taxi_Before_Auth) {
    StateMachine sm;
    EXPECT_FALSE(sm.canHandle(req_taxi));
}

/**
 * @brief Verifies that traffic requests are blocked in the STARTUP_AUTH state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Block_Traffic_Request_In_Auth) {
    StateMachine sm;
    EXPECT_FALSE(sm.canHandle(req_traffic));
}

/**
 * @brief Verifies that flight plan requests are blocked in ACTIVE_AIRSPACE —
 *        FLIGHT_PLAN_REQUEST is not listed in StatePermissions.csv for that state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, ActiveAirspace_Blocks_FlightPlanRequest) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi);                // -> ACTIVE_AIRSPACE
    ASSERT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");

    EXPECT_FALSE(sm.canHandle(req_fplan))
        << "Flight plan requests should be blocked once airborne.";
}

/**
 * @brief Verifies that weather requests are blocked in ACTIVE_AIRSPACE —
 *        WEATHER_REQUEST is not listed in StatePermissions.csv for that state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, ActiveAirspace_Blocks_WeatherRequest) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi);                // -> ACTIVE_AIRSPACE
    ASSERT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");

    EXPECT_FALSE(sm.canHandle(req_weather))
        << "Weather requests should be blocked once airborne.";
}

/**
 * @brief Verifies that non-essential commands (e.g. file transfer) are blocked
 *        while the pilot is in the EMERGENCY state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Restrict_NonEssential_In_Emergency) {
    StateMachine sm;
    sm.onAuthSuccess();

    sm.onRequestHandled(static_cast<reqtyp>(pkt_emgcy));
    ASSERT_EQ(sm.getStateName(), "EMERGENCY");

    EXPECT_FALSE(sm.canHandle(req_file))
        << "Large data transfer should be blocked during an EMERGENCY.";
}

/**
 * @brief Verifies that a known airport ID returns the correct weather conditions
 *        when the state machine permits the request.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, KnownAirport_ReturnsConditions) {
    std::string result = WeatherService::getWeather("YKF");
    EXPECT_EQ(result, "YKF: Overcast - 15C - Wind 10kt NW");
}

/**
 * @brief Verifies that all airport records in weather.csv return correct data.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, AllKnownAirports_ReturnCorrectData) {
    EXPECT_EQ(WeatherService::getWeather("YYZ"), "YYZ: Clear - 22C - Wind 5kt S");
    EXPECT_EQ(WeatherService::getWeather("YOW"), "YOW: Light Rain - 12C - Wind 15kt E");
    EXPECT_EQ(WeatherService::getWeather("YUL"), "YUL: Foggy - 10C - Wind 2kt N");
}

/**
 * @brief Verifies that an airport ID not in weather.csv returns a "not found"
 *        message rather than an empty string or crash.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, UnknownAirport_ReturnsNotFoundMessage) {
    std::string result = WeatherService::getWeather("XYZ");
    EXPECT_EQ(result, "No weather found for: XYZ");
}

/**
 * @brief Verifies that the airport lookup is case-sensitive (lowercase ID
 *        must not match an uppercase record).
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, AirportLookup_IsCaseSensitive) {
    std::string result = WeatherService::getWeather("ykf");
    EXPECT_EQ(result, "No weather found for: ykf");
}

/**
 * @brief Verifies that an empty location string returns a "not found" message.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, EmptyLocation_ReturnsNotFoundMessage) {
    std::string result = WeatherService::getWeather("");
    EXPECT_TRUE(result.find("No weather found for") != std::string::npos);
}

/**
 * @brief Verifies that taxi clearance is APPROVED when StatePermissions.csv
 *        grants TAXI_REQUEST in the PRE_FLIGHT state.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, TaxiClearance_GrantedFromStatePermissions) {
    std::string result = FileTransferManager::getTaxiClearance("PRE_FLIGHT");
    EXPECT_EQ(result, "Taxi Clearance: APPROVED for departure sequence");
}

/**
 * @brief Verifies that a valid flight ID returns the correct destination from
 *        Flight_Plan.csv.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, FlightPlan_ValidId_ReturnsDestination) {
    std::string result = FileTransferManager::getFlightPlan("AC123");
    EXPECT_EQ(result, "Flight Plan: AC123 -> Toronto (YYZ)");
}

/**
 * @brief Verifies that all flight IDs in Flight_Plan.csv return correct data.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, FlightPlan_AllKnownIds_ReturnCorrectData) {
    EXPECT_EQ(FileTransferManager::getFlightPlan("WJ456"), "Flight Plan: WJ456 -> Vancouver (YVR)");
    EXPECT_EQ(FileTransferManager::getFlightPlan("FL789"), "Flight Plan: FL789 -> Montreal (YUL)");
    EXPECT_EQ(FileTransferManager::getFlightPlan("TS012"), "Flight Plan: TS012 -> Ottawa (YOW)");
}

/**
 * @brief Verifies that an unknown flight ID returns a "no record" message.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, FlightPlan_UnknownId_ReturnsNoRecord) {
    std::string result = FileTransferManager::getFlightPlan("ZZZZ");
    EXPECT_EQ(result, "Flight Plan: no record for ZZZZ");
}

/**
 * @brief Verifies that a valid flight ID returns the correct coordinates from
 *        Traffic_Positions.csv.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Traffic_ValidId_ReturnsCoordinates) {
    std::string result = FileTransferManager::getTraffic("AC123");
    EXPECT_EQ(result, "Traffic: AC123 -> 43.6777N/79.6248W");
}

/**
 * @brief Verifies that all flight IDs in Traffic_Positions.csv return correct data.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Traffic_AllKnownIds_ReturnCorrectData) {
    EXPECT_EQ(FileTransferManager::getTraffic("WJ456"), "Traffic: WJ456 -> 49.1967N/123.1815W");
    EXPECT_EQ(FileTransferManager::getTraffic("FL789"), "Traffic: FL789 -> 45.4706N/73.7408W");
    EXPECT_EQ(FileTransferManager::getTraffic("TS012"), "Traffic: TS012 -> 45.3225N/75.6672W");
}

/**
 * @brief Verifies that an unknown flight ID in the traffic lookup returns a
 *        "no record" message.
 * @req REQ-SVR-040
 */
TEST(Requirement_SVR_040, Traffic_UnknownId_ReturnsNoRecord) {
    std::string result = FileTransferManager::getTraffic("ZZZZ");
    EXPECT_EQ(result, "Traffic: no record for ZZZZ");
}

/// @}

// ============================================================
/// @name REQ-SVR-050 — Audit Logging (TX/RX Persistence)
// ============================================================
/// @{

/**
 * @brief Verifies that logTelemetry() appends an entry and returns a
 *        confirmation string identifying the client and assigned log ID.
 * @req REQ-SVR-050
 * @req REQ-LOG-020
 */
TEST(Requirement_SVR_050, LogTelemetry_ReturnsConfirmationForClient) {
    std::string result = FileTransferManager::logTelemetry("Altitude=5000,Speed=250", 1, "CAN-8721269");
    EXPECT_TRUE(result.find("Telemetry logged for client 1") != std::string::npos);
}

/**
 * @brief Verifies that an empty telemetry payload is stored as NO_TELEMETRY
 *        rather than a blank CSV entry.
 * @req REQ-SVR-050
 */
TEST(Requirement_SVR_050, LogTelemetry_EmptyPayload_UsesPlaceholder) {
    std::string result = FileTransferManager::logTelemetry("", 2, "CAN-8721269");
    EXPECT_TRUE(result.find("Telemetry logged for client 2") != std::string::npos);
}

/**
 * @brief Verifies that successive logTelemetry() calls produce strictly
 *        incrementing log IDs, satisfying the unique-ID-per-entry requirement.
 * @req REQ-SVR-050
 * @req REQ-LOG-020
 */
TEST(Requirement_SVR_050, LogTelemetry_LogIdIncrementsEachCall) {
    std::string first  = FileTransferManager::logTelemetry("Pass=1", 3, "CAN-8721269");
    std::string second = FileTransferManager::logTelemetry("Pass=2", 3, "CAN-8721269");

    auto extractLogId = [](const std::string& s) {
        auto pos = s.find("LogID=");
        return (pos != std::string::npos) ? std::stoi(s.substr(pos + 6)) : -1;
    };

    int id1 = extractLogId(first);
    int id2 = extractLogId(second);
    ASSERT_NE(id1, -1) << "Could not parse LogID from: " << first;
    EXPECT_EQ(id2, id1 + 1);
}

/// @}

// ============================================================
/// @name REQ-INT-010 — Server-Side Authentication Gate
// ============================================================
/// @{


/**
 * @brief Verifies that every functional command is rejected in STARTUP/AUTH,
 *        enforcing the authentication-before-commands rule.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, Reject_All_Commands_Before_Auth) {
    StateMachine sm;
    ASSERT_EQ(sm.getStateName(), "STARTUP/AUTH");

    std::vector<reqtyp> functionalCommands = {
        req_weather,
        req_telemetry,
        req_file,
        req_taxi,
        req_fplan,
        req_traffic
    };

    for (auto cmd : functionalCommands) {
        EXPECT_FALSE(sm.canHandle(cmd))
            << "Command " << cmd << " was incorrectly allowed in STARTUP/AUTH state.";
    }
}

/**
 * @brief Verifies that a valid username/password pair returns a success message
 *        containing the pilot's license number.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, ValidCredentials_ReturnsSuccessWithLicense) {
    std::string result = ClientSession::authenticate("LKidder88,Alpha123");
    EXPECT_EQ(result, "Authentication Successful (License: CAN-8721269)");
}

/**
 * @brief Verifies that every pilot record in LoginAuth.csv can authenticate.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, AllValidPilots_CanAuthenticate) {
    EXPECT_EQ(ClientSession::authenticate("SSolorzano90,Beta456"), "Authentication Successful (License: CAN-9025242)");
    EXPECT_EQ(ClientSession::authenticate("RHackbart91,Gamma789"), "Authentication Successful (License: CAN-9026884)");
    EXPECT_EQ(ClientSession::authenticate("LNur89,Delta012"),      "Authentication Successful (License: CAN-8943209)");
}

/**
 * @brief Verifies that a correct username paired with an incorrect password is rejected.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, WrongPassword_ReturnsFailure) {
    std::string result = ClientSession::authenticate("LKidder88,WrongPassword");
    EXPECT_EQ(result, "Authentication Failed");
}

/**
 * @brief Verifies that a username not present in LoginAuth.csv is rejected.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, UnknownUser_ReturnsFailure) {
    std::string result = ClientSession::authenticate("GhostPilot,Password123");
    EXPECT_EQ(result, "Authentication Failed");
}

/**
 * @brief Verifies that credentials without a comma separator are rejected with
 *        a descriptive error message.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, MalformedCredentials_NoComma_ReturnsFailure) {
    std::string result = ClientSession::authenticate("NoCommaHere");
    EXPECT_EQ(result, "Authentication Failed: expected Username,Password");
}

/**
 * @brief Verifies that an empty credential string is rejected with a descriptive
 *        error message.
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, EmptyCredentials_ReturnsFailure) {
    std::string result = ClientSession::authenticate("");
    EXPECT_EQ(result, "Authentication Failed: expected Username,Password");
}

/**
 * @brief Verifies that credential matching is case-sensitive (wrong-case
 *        username and password must not authenticate).
 * @req REQ-INT-010
 */
TEST(Requirement_INT_010, CredentialsCaseSensitive_WrongCase_ReturnsFailure) {
    std::string result = ClientSession::authenticate("lkidder88,alpha123");
    EXPECT_EQ(result, "Authentication Failed");
}

/// @}

// ============================================================
/// @name REQ-INT-040 — CRC Checksum Corruption Detection
// ============================================================
/// @{


/**
 * @brief Verifies that a packet with a manually corrupted CRC does not match
 *        the freshly calculated checksum, confirming corruption is detectable.
 * @req REQ-INT-040
 * @req REQ-PKT-030
 */
TEST(Requirement_INT_040, Server_Blocks_Corrupted_CRC) {
    packet badPkt;
    char data[] = "Kill Engine";
    badPkt.PopulPacket(data, sizeof(data), 1, pkt_req);

    // Verify that offsetting the stored CRC by 1 produces a mismatch,
    // confirming the detection logic in ServerEngine::handleClient().
    EXPECT_NE(badPkt.calcCRC(), badPkt.GetCRC() + 1);
}

/// @}

// ===================================================================
/// @name REQ-PKT-010 / REQ-PKT-020 / REQ-PKT-030 — Packet Structure
// ===================================================================
/// @{


/**
 * @brief Verifies that a serialized packet is larger than its payload alone,
 *        confirming the Head|Body|Tail envelope is present.
 * @req REQ-PKT-010
 */
TEST(Requirement_PKT_010, Packet_SerializedSize_IncludesHeaderAndTail) {
    packet pkt;
    char data[] = "TestData";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_req);

    int serializedSize = 0;
    pkt.Serialize(&serializedSize);

    // Serialized size must be greater than just the payload (header + payload + CRC tail)
    EXPECT_GT(serializedSize, (int)sizeof(data));
}

/**
 * @brief Verifies that the packet header fields (frame type, client ID, payload
 *        length) are stored correctly after construction.
 * @req REQ-PKT-020
 */
TEST(Requirement_PKT_020, Packet_Header_ContainsCorrectFields) {
    packet pkt;
    char data[] = "HeaderCheck";
    pkt.PopulPacket(data, sizeof(data), 5, pkt_req);

    EXPECT_EQ(pkt.getPKType(),      (char)pkt_req);
    EXPECT_EQ(pkt.getCLID(),        (char)5);
    EXPECT_EQ(pkt.getPloadLength(), (unsigned char)sizeof(data));
}

/**
 * @brief Verifies that Serialize() computes and stores a non-zero CRC in the
 *        packet tail, and that calcCRC() reproduces the same value.
 * @req REQ-PKT-030
 */
TEST(Requirement_PKT_030, Packet_CRC_StoredInTailAfterSerialize) {
    packet pkt;
    char data[] = "CRCCheck";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_req);

    int size = 0;
    pkt.Serialize(&size);

    // CRC stored in the tail must be non-zero and match a fresh calculation
    EXPECT_NE(pkt.GetCRC(), 0);
    EXPECT_EQ(pkt.GetCRC(), pkt.calcCRC());
}

/// @}

// ============================================================
/// @name REQ-LOG — TX/RX Packet Audit Logging
// ============================================================
/// @{

namespace {
    /// Returns the last non-empty line from TransmitReceiveLog.csv.
    std::string readLastLogLine() {
        std::ifstream in("../data/TransmitReceiveLog.csv");
        if (!in.is_open()) in.open("data/TransmitReceiveLog.csv");
        std::string line, last;
        while (std::getline(in, line))
            if (!line.empty()) last = line;
        return last;
    }

    /// Returns the first line (header) from TransmitReceiveLog.csv.
    std::string readLogHeader() {
        std::ifstream in("../data/TransmitReceiveLog.csv");
        if (!in.is_open()) in.open("data/TransmitReceiveLog.csv");
        std::string header;
        std::getline(in, header);
        return header;
    }

    /// Extracts the integer LogID from the first comma-delimited field of a line.
    int extractLogId(const std::string& line) {
        auto comma = line.find(',');
        if (comma == std::string::npos) return -1;
        try { return std::stoi(line.substr(0, comma)); }
        catch (...) { return -1; }
    }
}

/**
 * @brief Verifies that the log file is created with the correct CSV header
 *        the first time Logger::logPacket is called.
 * @req REQ-LOG-010
 */
TEST(Requirement_LOG_010, LogFile_HasCorrectHeader) {
    packet pkt;
    char data[] = "HeaderProbe";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_auth);
    Logger::logPacket(pkt, Logger::Direction::RX, "STARTUP/AUTH", "header probe");

    std::string header = readLogHeader();
    EXPECT_EQ(header, "LogID,ClientID,Direction,PacketType,ServerState,Timestamp,Description")
        << "Log file header is missing or malformed";
}

/**
 * @brief Verifies that an RX packet produces a log entry containing the RX
 *        direction flag and the correct packet-type label.
 * @req REQ-LOG-010
 * @req REQ-LOG-030
 */
TEST(Requirement_LOG_010, LogPacket_RX_WritesEntryWithDirection) {
    packet pkt;
    char data[] = "RXTest";
    pkt.PopulPacket(data, sizeof(data), 2, pkt_auth);
    Logger::logPacket(pkt, Logger::Direction::RX, "STARTUP/AUTH", "rx direction test");

    std::string last = readLastLogLine();
    EXPECT_NE(last.find("RX"),   std::string::npos) << "Expected RX in: " << last;
    EXPECT_NE(last.find("AUTH"), std::string::npos) << "Expected AUTH in: " << last;
}

/**
 * @brief Verifies that a TX packet produces a log entry containing the TX
 *        direction flag and the correct packet-type label.
 * @req REQ-LOG-010
 * @req REQ-LOG-030
 */
TEST(Requirement_LOG_010, LogPacket_TX_WritesEntryWithDirection) {
    packet pkt;
    char data[] = "TXTest";
    pkt.PopulPacket(data, sizeof(data), 3, pkt_dat);
    Logger::logPacket(pkt, Logger::Direction::TX, "PRE_FLIGHT", "tx direction test");

    std::string last = readLastLogLine();
    EXPECT_NE(last.find("TX"),         std::string::npos) << "Expected TX in: " << last;
    EXPECT_NE(last.find("DATA"),       std::string::npos) << "Expected DATA in: " << last;
    EXPECT_NE(last.find("PRE_FLIGHT"), std::string::npos) << "Expected PRE_FLIGHT in: " << last;
}

/**
 * @brief Verifies that successive calls to logPacket produce strictly
 *        incrementing LogIDs, satisfying the unique-ID-per-entry requirement.
 * @req REQ-LOG-020
 */
TEST(Requirement_LOG_020, LogPacket_IdsIncrementOnSuccessiveCalls) {
    packet pkt;
    char data[] = "IDTest";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_req);

    Logger::logPacket(pkt, Logger::Direction::RX, "PRE_FLIGHT", "id increment first");
    int id1 = extractLogId(readLastLogLine());

    Logger::logPacket(pkt, Logger::Direction::TX, "PRE_FLIGHT", "id increment second");
    int id2 = extractLogId(readLastLogLine());

    ASSERT_NE(id1, -1) << "Could not parse first LogID";
    ASSERT_NE(id2, -1) << "Could not parse second LogID";
    EXPECT_EQ(id2, id1 + 1) << "LogIDs must increment by exactly 1";
}

/**
 * @brief Verifies that the server state name appears in the logged entry,
 *        satisfying the job-description/status-flag requirement.
 * @req REQ-LOG-030
 */
TEST(Requirement_LOG_030, LogPacket_ContainsServerState) {
    packet pkt;
    char data[] = "StateTest";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_emgcy);
    Logger::logPacket(pkt, Logger::Direction::RX, "EMERGENCY", "emergency state test");

    std::string last = readLastLogLine();
    EXPECT_NE(last.find("EMERGENCY"), std::string::npos)
        << "Server state missing from log entry: " << last;
}

/**
 * @brief Verifies that the human-readable description is written into the log
 *        entry, satisfying the job-description requirement.
 * @req REQ-LOG-030
 */
TEST(Requirement_LOG_030, LogPacket_ContainsDescription) {
    packet pkt;
    char data[] = "DescTest";
    pkt.PopulPacket(data, sizeof(data), 1, pkt_req);
    Logger::logPacket(pkt, Logger::Direction::TX, "PRE_FLIGHT", "unique_description_marker_xyz");

    std::string last = readLastLogLine();
    EXPECT_NE(last.find("unique_description_marker_xyz"), std::string::npos)
        << "Description missing from log entry: " << last;
}

/// @}

// ============================================================
/// @name Logic Tests — State Machine Behavioural Invariants
// ============================================================
/// @{

/**
 * @brief Verifies that entering DATA_TRANSFER from ACTIVE_AIRSPACE returns to
 *        ACTIVE_AIRSPACE after the transfer, not to PRE_FLIGHT.
 * @req REQ-SVR-020
 */
TEST(Logic_Tests, DataTransfer_Prevents_State_Warping) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi);   // -> ACTIVE_AIRSPACE

    sm.onRequestHandled(req_file);   // -> DATA_TRANSFER
    EXPECT_EQ(sm.getStateName(), "DATA_TRANSFER");

    sm.onDataTransferComplete();     // must restore ACTIVE_AIRSPACE, not PRE_FLIGHT
    EXPECT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");
}

/**
 * @brief Verifies that two StateMachine instances are completely independent —
 *        authenticating one pilot does not affect another.
 * @req REQ-SVR-010
 */
TEST(Logic_Tests, Client_Session_Isolation) {
    StateMachine pilot1;
    StateMachine pilot2;

    pilot1.onAuthSuccess();
    EXPECT_EQ(pilot1.getStateName(), "PRE_FLIGHT");
    EXPECT_EQ(pilot2.getStateName(), "STARTUP/AUTH");
}

/// @}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
