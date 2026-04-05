/**
 * @file StateMachine.hpp
 * @brief Declaration of the StateMachine class.
 *
 * Tracks the server-side session state for a single client connection and
 * enforces request permissions by consulting @c StatePermissions.csv.
 *
 * @par Valid states
 * | State              | Meaning                                              |
 * |--------------------|------------------------------------------------------|
 * | @c STARTUP_AUTH    | Initial state; awaiting successful authentication.  |
 * | @c PRE_FLIGHT      | Authenticated; pre-departure operations permitted.  |
 * | @c ACTIVE_AIRSPACE | Taxi clearance granted; aircraft in active airspace.|
 * | @c DATA_TRANSFER   | Flight manual download in progress.                 |
 * | @c EMERGENCY       | Emergency declared; most requests suspended.        |
 *
 * @par Key transition events
 * | Method                  | Trigger                         | Transition              |
 * |-------------------------|---------------------------------|-------------------------|
 * | onAuthSuccess()         | Successful login                | → PRE_FLIGHT            |
 * | onRequestHandled(taxi)  | Departure clearance             | → ACTIVE_AIRSPACE       |
 * | onRequestHandled(taxi)  | Arrival clearance               | → PRE_FLIGHT            |
 * | onRequestHandled(file)  | Manual download started         | → DATA_TRANSFER         |
 * | onDataTransferComplete()| Manual download finished        | → previousState         |
 * | onEmergency()           | Emergency packet received       | → EMERGENCY             |
 * | onEmergencyResolved()   | Resolve request received        | → previousState         |
 */
#pragma once
#include <string>
#include "../shared/Packet.h"

/**
 * @class StateMachine
 * @brief Per-session state machine enforcing the aviation client protocol.
 *
 * Each StateMachine instance is owned by a single client session in
 * ServerEngine::handleClient() and is not shared between connections.
 *
 * Permitted requests per state are read from @c StatePermissions.csv at
 * runtime via canHandle(), so the permission matrix can be updated without
 * recompiling the server.  One hard-coded exception applies: @c req_file is
 * always permitted in @c ACTIVE_AIRSPACE regardless of the CSV contents.
 *
 * The @ref previousState member provides a single level of state history,
 * used to restore the pre-transfer or pre-emergency state after
 * onDataTransferComplete() or onEmergencyResolved() is called.
 */
class StateMachine {
public:
    /**
     * @enum ServerState
     * @brief Enumeration of all valid server session states.
     */
	enum class ServerState {
		STARTUP_AUTH,
		PRE_FLIGHT,
		ACTIVE_AIRSPACE,
		DATA_TRANSFER,
		EMERGENCY
	};
  /**
     * @brief Constructs a StateMachine in the @c STARTUP_AUTH state.
     *
     * Both @ref state and @ref previousState are initialised to
     * @c STARTUP_AUTH so that any restore operation before the first
     * transition returns to a safe state.
     */
	StateMachine();
    // -------------------------------------------------------------------------
    // Permission query
    // -------------------------------------------------------------------------
 
    /**
     * @brief Returns whether @p requestType is permitted in the current state.
     *
     * Applies a hard-coded exception before consulting @c StatePermissions.csv:
     * @c req_file is always permitted in @c ACTIVE_AIRSPACE so that the flight
     * manual can be downloaded mid-flight.  All other types are delegated to
     * commandAllowedForState().
     *
     * @param requestType The request type to validate.
     * @return @c true  if the request is permitted in the current state.
     * @return @c false if the request is denied.
     */
	bool canHandle(reqtyp requestType) const;
 
    // -------------------------------------------------------------------------
    // State transition events
    // -------------------------------------------------------------------------
 
    /**
     * @brief Transitions to @c PRE_FLIGHT after successful authentication.
     */ 
	void onAuthSuccess();
	    /**
     * @brief Updates state in response to a successfully completed request.
     *
     * Transition rules:
     * - @c req_taxi from @c PRE_FLIGHT      → @c ACTIVE_AIRSPACE (departure).
     * - @c req_taxi from @c ACTIVE_AIRSPACE → @c PRE_FLIGHT (arrival).
     * - @c req_file (any state)             → saves current state to
     *   @ref previousState and transitions to @c DATA_TRANSFER.
     * - @c req_telemetry / @c req_traffic   → forces @c ACTIVE_AIRSPACE.
     * - All other types leave the state unchanged.
     *
     * @param requestType The request type that was just handled successfully.
     */
	void onRequestHandled(reqtyp requestType);
	 
    /**
     * @brief Restores the state saved before the file transfer began.
     *
     * Should be called after a flight manual transfer completes successfully.
     * Restores @ref state from @ref previousState, which was saved by
     * onRequestHandled() when @c req_file was processed.
     */
	void onDataTransferComplete();
 
    /**
     * @brief Saves current state and transitions to @c EMERGENCY.
     *
     * Called on receipt of a @c pkt_emgcy packet.  The saved state is
     * restored by onEmergencyResolved().
     */
	void onEmergency();
   /**
     * @brief Restores the state saved before the emergency was declared.
     *
     * Called when a @c req_resolve request is received.  Restores @ref state
     * from @ref previousState, which was saved by onEmergency().
     */
	void onEmergencyResolved();

    // -------------------------------------------------------------------------
    // State accessor
    // -------------------------------------------------------------------------
 
    /**
     * @brief Returns the human-readable name of the current state.
     *
     * The returned string matches the @c StateName column in
     * @c StatePermissions.csv and the @c "STATE: <n>" stdout prefix
     * consumed by the ATCScreen dashboard.
     *
     * @return One of @c "STARTUP/AUTH", @c "PRE_FLIGHT", @c "ACTIVE_AIRSPACE",
     *         @c "DATA_TRANSFER", @c "EMERGENCY", or @c "UNKNOWN".
     */
	std::string getStateName() const;

private:
    /// @brief The current session state.
	ServerState state;
    /**
     * @brief The state active immediately before the most recent
     *        @c DATA_TRANSFER or @c EMERGENCY transition.
     *
     * Provides a single level of history used by onDataTransferComplete()
     * and onEmergencyResolved() to restore the prior state.
     */	
	ServerState previousState;
    /**
     * @brief Maps a @c reqtyp value to its @c StatePermissions.csv command string.
     *
     * @param requestType The request type to map.
     * @return The corresponding command string (e.g. @c "TAXI_REQUEST"),
     *         or @c "UNKNOWN" for unrecognised values.
     */
	std::string requestToCommand(reqtyp requestType) const;
    /**
     * @brief Checks whether @p command is listed as permitted for the current state
     *        in @c StatePermissions.csv.
     *
     * @param command A command string as returned by requestToCommand().
     * @return @c true  if the command is found in the current state's permission list.
     * @return @c false if the file cannot be opened, the state has no row, or
     *                  the command is absent.
     */	
	bool commandAllowedForState(const std::string& command) const;
};
