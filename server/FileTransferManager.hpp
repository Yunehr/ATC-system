/**
 * @file FileTransferManager.hpp
 * @brief Declaration of the FileTransferManager class.
 *
 * Provides static server-side data access methods for flight plans, air
 * traffic positions, taxi/landing clearances, telemetry logging, and flight
 * manual availability.  All methods are stateless and read from or write to
 * CSV/PDF files located in the shared @c data/ directory.
 *
 * @par Data files used
 * | File                     | Access       | Method(s)                  |
 * |--------------------------|--------------|----------------------------|
 * | @c Flight_Plan.csv       | Read         | getFlightPlan()            |
 * | @c Traffic_Positions.csv | Read         | getTraffic()               |
 * | @c StatePermissions.csv  | Read         | getTaxiClearance()         |
 * | @c SystemTrafficLog.csv  | Read + Append| logTelemetry()             |
 * | @c FlightManual.pdf      | Read (binary)| getManualInfo()            |
 */
#pragma once
#include <string>

/**
 * @class FileTransferManager
 * @brief Static utility class for server-side data file access.
 *
 * All methods are static; FileTransferManager is not intended to be
 * instantiated.  File paths are resolved at call time using a two-path
 * probe (@c ../data/ then @c data/) so the same binary works from both
 * the project root and the build output directory.
 */
class FileTransferManager {
public:
 
    /**
     * @brief Retrieves the flight plan for the specified flight ID.
     *
     * Searches @c Flight_Plan.csv for a row whose ID column matches
     * @p flightId.
     *
     * @param flightId Flight identifier to look up (e.g. @c "AC123").
     * @return @c "Flight Plan: <id> -> <value>" on success, or an error
     *         description string on failure.
     */
	static std::string getFlightPlan(const std::string& flightId);
	 
    /**
     * @brief Retrieves air traffic position data for the specified flight ID.
     *
     * Searches @c Traffic_Positions.csv for a matching row.  Pass an empty
     * @p flightId to retrieve all records joined by @c " | ".
     *
     * @param flightId Flight identifier to look up, or empty for all records.
     * @return @c "Traffic: <id> -> <value>" on success, all records joined
     *         by @c " | " when @p flightId is empty, or an error description.
     */
	static std::string getTraffic(const std::string& flightId);
	 
    /**
     * @brief Checks @c StatePermissions.csv to determine whether taxi or
     *        landing clearance should be granted in the given server state.
     *
     * Grants clearance only if @c TAXI_REQUEST appears in the dash-delimited
     * command list for @p currentState.  The response string distinguishes
     * between departure and arrival clearance so the Python UI can navigate
     * to the correct page.
     *
     * @param currentState The server's current state string
     *                     (e.g. @c "PRE_FLIGHT", @c "ACTIVE_AIRSPACE").
     * @return One of:
     *         - @c "Taxi Clearance: APPROVED for departure sequence"
     *         - @c "Landing Clearance: APPROVED for arrival sequence"
     *         - @c "Taxi Clearance: DENIED by state policy"
     *         - @c "Taxi Clearance: unable to verify permissions"
     */
	static std::string getTaxiClearance(const std::string& currentState);
	    /**
     * @brief Appends a telemetry record to @c SystemTrafficLog.csv.
     *
     * Seeds the next LogID from the highest ID already in the file, then
     * extracts @c Lat and @c Long from @p telemetry and writes them as
     * discrete columns.  An empty @p telemetry string is recorded as
     * @c "NO_TELEMETRY".
     *
     * @param telemetry Raw telemetry payload
     *                  (e.g. @c "Lat=51.5,Long=-0.1,Alt=10000,Speed=450"),
     *                  or empty to log a placeholder entry.
     * @param clientId  Numeric client identifier included in the confirmation message.
     * @param pilotId   Pilot identifier written to the @c PilotID CSV column.
     * @return A confirmation string (e.g. @c "Telemetry logged for client 1 (LogID=42)"),
     *         or @c "Telemetry: failed to open SystemTrafficLog.csv" on failure.
     */
	static std::string logTelemetry(const std::string& telemetry, unsigned char clientId, const std::string& pilotId);
	   /**
     * @brief Checks whether @c FlightManual.pdf is available and reports its size.
     *
     * @return @c "Flight Manual ready. Size=<N> bytes" on success, or
     *         @c "Flight Manual: file not found" if the file could not be opened.
     */
	static std::string getManualInfo();

private:
    /**
     * @brief Searches a two-column @c "ID,Value" CSV for a record matching @p flightId.
     *
     * Shared implementation used by getFlightPlan() and getTraffic().  If
     * @p flightId is empty, all rows are returned joined by @c " | ".
     *
     * @param csvPath  Path or bare filename of the CSV to search.
     * @param flightId Flight ID to match, or empty to return all records.
     * @param label    Prefix string used in all returned messages.
     * @return A formatted result string, or an error description.
     */
	static std::string findByFlightId(const std::string& csvPath, const std::string& flightId, const std::string& label);
};
