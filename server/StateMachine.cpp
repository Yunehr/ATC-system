/**
 * @file StateMachine.cpp
 * @brief Implementation of the StateMachine class.
 *
 * Tracks the server-side session state for a single client connection and
 * enforces command permissions by consulting @c StatePermissions.csv.
 *
 * @par State transition diagram
 * @code
 *                    ┌─────────────────┐
 *               ┌───►│  STARTUP/AUTH   │◄──────────────────────┐
 *               │    └────────┬────────┘                       │
 *               │             │ onAuthSuccess()                │
 *               │             ▼                                │
 *               │    ┌─────────────────┐   req_file            │
 *               │    │   PRE_FLIGHT    │──────────────►┐       │
 *               │    └────────┬────────┘               │       │
 *               │   req_taxi  │ req_taxi               │       │
 *               │   (arrival) │ (departure)            │       │
 *               │             ▼                        │       │
 *               │    ┌─────────────────┐   req_file   │       │
 *               │    │ ACTIVE_AIRSPACE │──────────────►│       │
 *               │    └────────┬────────┘               │       │
 *        resolve│   emergency │                        ▼       │
 *               │             ▼              ┌──────────────────┤
 *               │    ┌─────────────────┐     │  DATA_TRANSFER  │
 *               └────│    EMERGENCY    │     └─────────────────►┘
 *                    └─────────────────┘   onDataTransferComplete()
 * @endcode
 *
 * @par StatePermissions.csv format
 * Two-column CSV (with a header row) mapping state names to a
 * hyphen-delimited list of permitted command strings:
 * @code
 * StateName, Commands
 * PRE_FLIGHT, WEATHER_REQUEST-FLIGHT_PLAN_REQUEST-TAXI_REQUEST-FILE_REQUEST
 * @endcode
 */
 

#include "StateMachine.hpp"
#include <fstream>
#include <sstream>

// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Opens a data file by probing two candidate directories.
 *
 * Tries @c ../data/<fileName> first, then falls back to @c data/<fileName>.
 *
 * @param fileName Bare filename to open (e.g. @c "StatePermissions.csv").
 * @return An @c std::ifstream that is open on success, or in a failed state
 *         if the file was not found in either location.
 */
static std::ifstream openDataFile(const std::string& fileName) {
	std::ifstream in("../data/" + fileName);
	if (in.is_open()) return in;
	in.open("data/" + fileName);
	return in;
}

// ---------------------------------------------------------
// Construction
// ---------------------------------------------------------
 
/**
 * @brief Constructs a StateMachine in the initial @c STARTUP_AUTH state.
 *
 * Both @ref state and @ref previousState are initialised to
 * @c ServerState::STARTUP_AUTH so that the first restore (e.g. from an
 * emergency declared before authentication) returns to a safe state.
 */
StateMachine::StateMachine()
	: state(ServerState::STARTUP_AUTH), previousState(ServerState::STARTUP_AUTH) {}
// ---------------------------------------------------------
// State queries
// ---------------------------------------------------------
 
/**
 * @brief Returns the human-readable name of the current state.
 *
 * The returned string matches the @c StateName column values in
 * @c StatePermissions.csv and the @c "STATE: <n>" prefix emitted to
 * stdout for the ATCScreen dashboard.
 *
 * @return One of @c "STARTUP/AUTH", @c "PRE_FLIGHT", @c "ACTIVE_AIRSPACE",
 *         @c "DATA_TRANSFER", @c "EMERGENCY", or @c "UNKNOWN".
 */
std::string StateMachine::getStateName() const {
	switch (state) {
	case ServerState::STARTUP_AUTH: return "STARTUP/AUTH";
	case ServerState::PRE_FLIGHT: return "PRE_FLIGHT";
	case ServerState::ACTIVE_AIRSPACE: return "ACTIVE_AIRSPACE";
	case ServerState::DATA_TRANSFER: return "DATA_TRANSFER";
	case ServerState::EMERGENCY: return "EMERGENCY";
	}
	return "UNKNOWN";
}

// ---------------------------------------------------------
// Permission helpers
// ---------------------------------------------------------
 
/**
 * @brief Maps a @c reqtyp enumeration value to its @c StatePermissions.csv command string.
 *
 * The returned string is compared against the hyphen-delimited command list
 * in @c StatePermissions.csv by commandAllowedForState().
 *
 * @param requestType The request type to map.
 * @return The corresponding command string, or @c "UNKNOWN" for unrecognised values.
 */
std::string StateMachine::requestToCommand(reqtyp requestType) const {
	switch (requestType) {
	case req_weather: return "WEATHER_REQUEST";
	case req_telemetry: return "TELEMETRY_UPDATE";
	case req_file: return "FILE_REQUEST";
	case req_taxi: return "TAXI_REQUEST";
	case req_fplan: return "FLIGHT_PLAN_REQUEST";
	case req_traffic: return "TRAFFIC_REQUEST";
	case req_resolve: return "RESOLVE_REQUEST";
	default: return "UNKNOWN";
	}
}
/**
 * @brief Checks whether @p command is permitted in the current state.
 *
 * Opens @c StatePermissions.csv, finds the row matching the current state
 * name, and searches its hyphen-delimited command list for @p command.
 *
 * @param command A command string as returned by requestToCommand()
 *                (e.g. @c "TAXI_REQUEST").
 * @return @c true  if the command appears in the permissions list for the
 *                  current state.
 * @return @c false if the file could not be opened, the state has no row,
 *                  or the command is not in the list.
 */
bool StateMachine::commandAllowedForState(const std::string& command) const {
	std::ifstream in = openDataFile("StatePermissions.csv");
	if (!in.is_open()) {
		return false;
	}

	const std::string currentState = getStateName();
	std::string line;
	std::getline(in, line); // header

	while (std::getline(in, line)) {
		std::stringstream ss(line);
		std::string stateName;
		std::string commandList;

		if (!std::getline(ss, stateName, ',')) continue;
		if (!std::getline(ss, commandList)) continue;

		if (stateName == currentState) {
			// Parse commands separated by hyphens
			std::stringstream cmdStream(commandList);
			std::string cmd;
			while (std::getline(cmdStream, cmd, '-')) {
				if (cmd == command) {
					return true;
				}
			}
		}
	}
	return false;
}

/**
 * @brief Returns whether the state machine can currently handle @p requestType.
 *
 * Applies a hard-coded exception before consulting @c StatePermissions.csv:
 * @c req_file is always permitted in @c ACTIVE_AIRSPACE regardless of the
 * CSV contents, because the manual may need to be downloaded mid-flight.
 *
 * All other request types are checked via commandAllowedForState().
 *
 * @param requestType The incoming request type to validate.
 * @return @c true  if the request is permitted in the current state.
 * @return @c false if the request is denied.
 */
bool StateMachine::canHandle(reqtyp requestType) const {
	if (requestType == req_file && state == ServerState::ACTIVE_AIRSPACE) {
		return true;
	}

	return commandAllowedForState(requestToCommand(requestType));
}

 
// ---------------------------------------------------------
// State transition events
// ---------------------------------------------------------
 
/**
 * @brief Transitions the state machine to @c PRE_FLIGHT after successful authentication.
 *
 * Called by ServerEngine::handleClient() when ClientSession::authenticate()
 * returns a success response.
 */
void StateMachine::onAuthSuccess() {
	state = ServerState::PRE_FLIGHT;
}

/**
 * @brief Updates the state in response to a successfully handled request.
 *
 * Applies the following transition rules:
 * - @c req_taxi from @c PRE_FLIGHT     → @c ACTIVE_AIRSPACE (departure).
 * - @c req_taxi from @c ACTIVE_AIRSPACE → @c PRE_FLIGHT (arrival/landing).
 * - @c req_file (any state)            → saves current state to @ref previousState
 *   and transitions to @c DATA_TRANSFER.
 * - @c req_telemetry / @c req_traffic  → forces state to @c ACTIVE_AIRSPACE.
 * - All other request types leave the state unchanged.
 *
 * @param requestType The request type that was just handled successfully.
 */
void StateMachine::onRequestHandled(reqtyp requestType) {
	if (requestType == req_taxi) {
		if (state == ServerState::PRE_FLIGHT) {
			state = ServerState::ACTIVE_AIRSPACE;
			return;
		}
		if (state == ServerState::ACTIVE_AIRSPACE) {
			state = ServerState::PRE_FLIGHT;
			return;
		}
	}

	if (requestType == req_file) {
		previousState = state;
		state = ServerState::DATA_TRANSFER;
		return;
	}

	if (requestType == req_telemetry || requestType == req_traffic) {
		state = ServerState::ACTIVE_AIRSPACE;
	}
}
/**
 * @brief Restores the state that was active before a file transfer began.
 *
 * Should be called after sendFlightManual() completes successfully.
 * Restores @ref state from @ref previousState, which was saved by
 * onRequestHandled() when @c req_file was processed.
 */
void StateMachine::onDataTransferComplete() {
	state = previousState;
}
/**
 * @brief Transitions the state machine into the @c EMERGENCY state.
 *
 * Saves the current state to @ref previousState so it can be restored by
 * onEmergencyResolved().  Called by ServerEngine::handleClient() on receipt
 * of a @c pkt_emgcy packet.
 */
void StateMachine::onEmergency() {
	previousState = state;
	state = ServerState::EMERGENCY;
}
/**
 * @brief Restores the state that was active before the emergency was declared.
 *
 * Called by ServerEngine::handleClient() when a @c req_resolve request is
 * received.  Restores @ref state from @ref previousState, which was saved
 * by onEmergency().
 */
void StateMachine::onEmergencyResolved() {
	state = previousState;
}
