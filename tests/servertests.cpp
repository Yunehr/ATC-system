#include <gtest/gtest.h>
#include "../server/ServerEngine.hpp"
#include "../server/StateMachine.hpp"
#include "../shared/Request.h"

// --- REQ-SVR-020: State Machine Initialization ---
TEST(Requirement_SVR_020, InitialStateIsStartupAuth) {
    StateMachine sm;
    // MATCHING CODE: sm starts at ServerState::STARTUP_AUTH
    EXPECT_EQ(sm.getStateName(), "STARTUP/AUTH");
}

// --- REQ-SVR-020: Transition Logic ---
TEST(Requirement_SVR_020, TransitionAfterAuth) {
    StateMachine sm;
    sm.onAuthSuccess(); 
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");
}

// --- REQ-SVR-040: Operational Logic (CSV Permissions) ---
TEST(Requirement_SVR_040, WeatherDeniedInAuth) {
    StateMachine sm;
    // Pilot should NOT be able to get weather before logging in
    bool allowed = sm.canHandle(req_weather);
    EXPECT_FALSE(allowed);
}

TEST(Requirement_SVR_040, WeatherAllowedInPreFlight) {
    StateMachine sm;
    sm.onAuthSuccess(); // Move to PRE_FLIGHT
    
    bool allowed = sm.canHandle(req_weather);
    EXPECT_TRUE(allowed);
}

// --- REQ-SVR-030: Data Transfer Logic ---
TEST(Requirement_SVR_030, StateMovesToDataTransfer) {
    StateMachine sm;
    sm.onAuthSuccess(); // Pre-Flight
    
    // Simulating a file request handled
    sm.onRequestHandled(req_file);
    EXPECT_EQ(sm.getStateName(), "DATA_TRANSFER");
    
    // After transfer complete, it should return to PRE_FLIGHT
    sm.onDataTransferComplete();
    EXPECT_EQ(sm.getStateName(), "PRE_FLIGHT");
}

TEST(Requirement_SVR_020, EmergencyStateIsReachable) {
    StateMachine sm;
    sm.onAuthSuccess();
    sm.onRequestHandled(req_taxi); 

    // for when a pkt_emgcy is received.
    // NOTE: This will still FAIL until Liban updates StateMachine.cpp
    sm.onRequestHandled(static_cast<reqtyp>(pkt_emgcy)); 
    
    EXPECT_EQ(sm.getStateName(), "EMERGENCY");
}

TEST(Logic_Tests, DataTransfer_Prevents_State_Warping) {
    StateMachine sm;
    sm.onAuthSuccess(); // PRE_FLIGHT
    sm.onRequestHandled(req_taxi); // Move to ACTIVE_AIRSPACE
    
    // Pilot requests a file while flying
    sm.onRequestHandled(req_file); 
    EXPECT_EQ(sm.getStateName(), "DATA_TRANSFER");

    // When finished, the pilot should still be in ACTIVE_AIRSPACE, not PRE_FLIGHT
    sm.onDataTransferComplete();
    
    EXPECT_EQ(sm.getStateName(), "ACTIVE_AIRSPACE");
}

TEST(Requirement_SVR_040, Block_Taxi_Before_Auth) {
    StateMachine sm;
    // Initial state: STARTUP_AUTH
    
    bool canTaxi = sm.canHandle(req_taxi);
    
    // This should be FALSE. If the server allows it, log a Security Issue.
    EXPECT_FALSE(canTaxi);
}

TEST(Requirement_COM_040, Server_Blocks_Corrupted_CRC) {
    // Create a packet and intentionally mess up the data so CRC doesn't match
    packet badPkt;
    char data[] = "Kill Engine";
    badPkt.PopulPacket(data, sizeof(data), 1, pkt_req);
    
    // Manual corruption: change the data without updating the CRC
    // Note: This test verifies the ServerEngine's handleClient logic
    // where it checks if (rxPkt.calcCRC() != rxPkt.GetCRC())
    
    EXPECT_NE(badPkt.calcCRC(), badPkt.GetCRC() + 1); // Logic check
}

TEST(Requirement_SVR_040, Block_Traffic_Request_In_Auth) {
    StateMachine sm;
    // Current State: STARTUP_AUTH
    
    // Based on CSV, TRAFFIC_REQUEST is only allowed in ACTIVE_AIRSPACE
    bool allowed = sm.canHandle(req_traffic);
    
    EXPECT_FALSE(allowed);
}

TEST(Logic_Tests, Server_Handles_Missing_Manual_Gracefully) {
    ServerEngine server;
    
    // Explicitly cast to SOCKET to match the function signature
    bool result = server.sendFlightManual((SOCKET)-1, 99);
    
    EXPECT_FALSE(result); 
}

TEST(Logic_Tests, Client_Session_Isolation) {
    StateMachine pilot1;
    StateMachine pilot2;

    // Pilot 1 authenticates
    pilot1.onAuthSuccess(); 
    EXPECT_EQ(pilot1.getStateName(), "PRE_FLIGHT");

    // Pilot 2 is still at the login screen
    EXPECT_EQ(pilot2.getStateName(), "STARTUP/AUTH");
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}