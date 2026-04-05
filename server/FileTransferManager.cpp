/**
 * @file FileTransferManager.cpp
 * @brief Implementation of the FileTransferManager class.
 *
 * Provides server-side data access for flight plans, air traffic, taxi
 * clearances, telemetry logging, and flight manual availability checks.
 * All data files are located using a two-path probe (@c ../data/ then
 * @c data/) consistent with the rest of the server codebase.
 *
 * @par Data files used
 * | File                    | Access    | Purpose                              |
 * |-------------------------|-----------|--------------------------------------|
 * | @c Flight_Plan.csv      | Read      | Flight plan lookup by flight ID      |
 * | @c Traffic_Positions.csv| Read      | Air traffic lookup by flight ID      |
 * | @c StatePermissions.csv | Read      | Taxi/landing clearance policy        |
 * | @c SystemTrafficLog.csv | Read+Append | Telemetry logging                  |
 * | @c FlightManual.pdf     | Read (binary) | Manual availability check        |
 */
#include "FileTransferManager.hpp"
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <iomanip>

// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Opens a data file for reading by probing two candidate directories.
 *
 * Tries @c ../data/<fileName> first, then falls back to @c data/<fileName>.
 *
 * @param fileName Bare filename to open (e.g. @c "Flight_Plan.csv").
 * @param mode     @c std::ios::openmode flags forwarded to both open attempts.
 *                 Defaults to @c std::ios::in.
 * @return An @c std::ifstream that is open on success, or in a failed state
 *         if the file was not found in either location.
 */
static std::ifstream openDataRead(const std::string& fileName, std::ios::openmode mode = std::ios::in) {
	std::ifstream in("../data/" + fileName, mode);
	if (in.is_open()) return in;
	in.open("data/" + fileName, mode);
	return in;
}
/**
 * @brief Opens a data file for appending by probing two candidate directories.
 *
 * Tries @c ../data/<fileName> first, then falls back to @c data/<fileName>.
 * Always opens in @c std::ios::app mode so existing content is preserved.
 *
 * @param fileName Bare filename to open (e.g. @c "SystemTrafficLog.csv").
 * @return An @c std::ofstream that is open on success, or in a failed state
 *         if neither path could be opened.
 */
static std::ofstream openDataAppend(const std::string& fileName) {
	std::ofstream out("../data/" + fileName, std::ios::app);
	if (out.is_open()) return out;
	out.open("data/" + fileName, std::ios::app);
	return out;
}

// ---------------------------------------------------------
// FileTransferManager implementation
// ---------------------------------------------------------
 
/**
 * @brief Searches a two-column @c "ID,Value" CSV for a record matching @p flightId.
 *
 * If @p flightId is non-empty, returns the first matching row formatted as
 * @c "<label>: <id> -> <value>".  If @p flightId is empty, returns all rows
 * joined by @c " | " — useful for broadcasting full traffic tables.
 *
 * The @p csvPath is tried directly first.  If it cannot be opened, the bare
 * filename is extracted and re-tried via the two-path probe in openDataRead().
 * This allows callers to pass absolute-style paths (e.g. @c "../data/Foo.csv")
 * that also work when the working directory differs.
 *
 * @param csvPath  Path or filename of the CSV to search.
 * @param flightId Flight ID to look up, or empty string to return all records.
 * @param label    Prefix string used in all returned messages (e.g. @c "Flight Plan").
 * @return A formatted result string, or one of:
 *         - @c "<label>: data file not found"   if the file could not be opened.
 *         - @c "<label>: no record for <id>"    if no match was found (targeted lookup).
 *         - @c "<label>: no records"            if the CSV was empty (full listing).
 */
std::string FileTransferManager::findByFlightId(const std::string& csvPath, const std::string& flightId, const std::string& label) {
	std::ifstream in(csvPath);
	if (!in.is_open()) {
		const std::string nameOnly = csvPath.substr(csvPath.find_last_of('/') + 1);
		in = openDataRead(nameOnly);
	}

	if (!in.is_open()) {
		return label + ": data file not found";
	}

	std::string line;
	std::getline(in, line); // header

	std::vector<std::string> allRows;

	while (std::getline(in, line)) {
		std::stringstream ss(line);
		std::string id;
		std::string value;

		if (!std::getline(ss, id, ',')) continue;
		if (!std::getline(ss, value)) continue;

		if (!flightId.empty() && id == flightId) {
			return label + ": " + id + " -> " + value;
		}

		if (flightId.empty()) {
			allRows.push_back(id + " -> " + value);
		}
	}

	if (!flightId.empty()) {
		return label + ": no record for " + flightId;
	}

	if (allRows.empty()) {
		return label + ": no records";
	}

	std::string joined = label + ": ";
	for (size_t i = 0; i < allRows.size(); ++i) {
		if (i > 0) joined += " | ";
		joined += allRows[i];
	}
	return joined;
}

/**
 * @brief Retrieves the flight plan for the specified flight ID.
 *
 * Delegates to findByFlightId() using @c Flight_Plan.csv.
 *
 * @param flightId Flight identifier to look up (e.g. @c "AC123").
 * @return Formatted flight plan string, or an error description.
 */
std::string FileTransferManager::getFlightPlan(const std::string& flightId) {
	return findByFlightId("../data/Flight_Plan.csv", flightId, "Flight Plan");
}

/**
 * @brief Retrieves air traffic position data for the specified flight ID.
 *
 * Delegates to findByFlightId() using @c Traffic_Positions.csv.  Pass an
 * empty @p flightId to retrieve all traffic records joined by @c " | ".
 *
 * @param flightId Flight identifier to look up, or empty for all records.
 * @return Formatted traffic string, or an error description.
 */
std::string FileTransferManager::getTraffic(const std::string& flightId) {
	return findByFlightId("../data/Traffic_Positions.csv", flightId, "Traffic");
}

/**
 * @brief Checks @c StatePermissions.csv to determine whether a taxi or
 *        landing clearance should be granted in the current server state.
 *
 * Reads the permissions CSV and searches for a row whose @c StateName column
 * matches @p currentState.  If the @c TAXI_REQUEST command is present in
 * that row's dash-delimited command list, clearance is granted.  The response
 * string distinguishes between departure (state @c PRE_FLIGHT) and arrival
 * (state @c ACTIVE_AIRSPACE) so the Python UI can navigate to the correct page.
 *
 * @param currentState The server's current state string (e.g. @c "PRE_FLIGHT",
 *                     @c "ACTIVE_AIRSPACE").
 * @return One of:
 *         - @c "Taxi Clearance: APPROVED for departure sequence"
 *         - @c "Landing Clearance: APPROVED for arrival sequence"
 *         - @c "Taxi Clearance: DENIED by state policy"
 *         - @c "Taxi Clearance: unable to verify permissions" if the file
 *           could not be opened.
 */
std::string FileTransferManager::getTaxiClearance(const std::string& currentState) {
	std::ifstream in = openDataRead("StatePermissions.csv");
	if (!in.is_open()) {
		return "Taxi Clearance: unable to verify permissions";
	}

	std::string line;
	std::getline(in, line); // header

	while (std::getline(in, line)) {
		std::stringstream ss(line);
		std::string stateName;
		std::string commandList;

		if (!std::getline(ss, stateName, ',')) continue;
		if (!std::getline(ss, commandList)) continue;

		if (stateName == currentState) {
			// Check if TAXI_REQUEST is in the comma-separated command list
			std::stringstream cmdStream(commandList);
			std::string cmd;
			while (std::getline(cmdStream, cmd, '-')) {
				if (cmd == "TAXI_REQUEST") {
					if (currentState == "ACTIVE_AIRSPACE") {
						return "Landing Clearance: APPROVED for arrival sequence";
					}
					return "Taxi Clearance: APPROVED for departure sequence";
				}
			}
		}
	}

	return "Taxi Clearance: DENIED by state policy";
}

// ---------------------------------------------------------
// Private helpers
// ---------------------------------------------------------
 
/**
 * @brief Extracts the value of a @c key=value field from a telemetry string.
 *
 * Searches for @c "<key>=" and returns everything up to the next comma or
 * end of string.  Used to pull individual fields (e.g. @c Lat, @c Long)
 * from the telemetry payload before writing them as discrete CSV columns.
 *
 * @param telemetry Full telemetry string (e.g. @c "Lat=51.5,Long=-0.1,Alt=10000,Speed=450").
 * @param key       Field name to search for (without the @c = sign).
 * @return The extracted value string, or @c "0" if the key was not found.
 */
static std::string parseField(const std::string& telemetry, const std::string& key) {
	std::string search = key + "=";
	auto pos = telemetry.find(search);
	if (pos == std::string::npos) return "0";
	pos += search.size();
	auto end = telemetry.find(',', pos);
	return (end == std::string::npos) ? telemetry.substr(pos) : telemetry.substr(pos, end - pos);
}
/**
 * @brief Appends a telemetry record to @c SystemTrafficLog.csv.
 *
 * Reads the current highest @c LogID from the file (if it exists) to ensure
 * the new entry receives a unique, monotonically increasing ID.  Extracts
 * @c Lat and @c Long fields from @p telemetry via parseField() and writes
 * them as discrete columns alongside the full telemetry string.
 *
 * @par CSV row format written
 * @code
 * LogID, PilotID, Lat, Long, Status, Telemetry, Timestamp
 * @endcode
 *
 * If @p telemetry is empty, the @c Telemetry column is written as
 * @c "NO_TELEMETRY" to avoid a blank CSV field.
 *
 * @param telemetry Raw telemetry string (e.g. @c "Lat=51.5,Long=-0.1,Alt=10000,Speed=450"),
 *                  or empty to log a placeholder entry.
 * @param clientId  Numeric client ID, included in the confirmation message.
 * @param pilotId   Pilot identifier written to the @c PilotID CSV column.
 * @return A confirmation string (e.g. @c "Telemetry logged for client 1 (LogID=42)"),
 *         or @c "Telemetry: failed to open SystemTrafficLog.csv" on failure.
 */
std::string FileTransferManager::logTelemetry(const std::string& telemetry, unsigned char clientId, const std::string& pilotId) {
	std::ifstream io = openDataRead("SystemTrafficLog.csv");
	int lastId = 0;

	if (io.is_open()) {
		std::string line;
		std::getline(io, line); // header
		while (std::getline(io, line)) {
			std::stringstream ss(line);
			std::string id;
			if (std::getline(ss, id, ',')) {
				try {
					int parsed = std::stoi(id);
					if (parsed > lastId) lastId = parsed;
				}
				catch (...) {
				}
			}
		}
		io.close();
	}

	std::ofstream out = openDataAppend("SystemTrafficLog.csv");
	if (!out.is_open()) {
		return "Telemetry: failed to open SystemTrafficLog.csv";
	}

	auto now = std::chrono::system_clock::now();
	std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

	std::ostringstream stamp;
	stamp << std::put_time(std::localtime(&nowTime), "%Y-%m-%d %H:%M:%S");

	const int nextId = lastId + 1;
	const std::string safeTelemetry = telemetry.empty() ? "NO_TELEMETRY" : telemetry;
	const std::string lat = parseField(telemetry, "Lat");
	const std::string lon = parseField(telemetry, "Long");
	out << nextId << "," << pilotId << "," << lat << "," << lon << ",ACTIVE," << safeTelemetry << "," << stamp.str() << "\n";

	std::ostringstream msg;
	msg << "Telemetry logged for client " << (int)clientId << " (LogID=" << nextId << ")";
	return msg.str();
}
/**
 * @brief Checks whether the flight manual PDF is available and reports its size.
 *
 * Opens @c FlightManual.pdf in binary mode with @c std::ios::ate so that
 * @c tellg() immediately returns the file size without reading the content.
 *
 * @return @c "Flight Manual ready. Size=<N> bytes" on success, or
 *         @c "Flight Manual: file not found" if the file could not be opened.
 */
std::string FileTransferManager::getManualInfo() {
	std::ifstream in = openDataRead("FlightManual.pdf", std::ios::binary | std::ios::ate);
	if (!in.is_open()) {
		return "Flight Manual: file not found";
	}

	std::streamsize size = in.tellg();
	std::ostringstream msg;
	msg << "Flight Manual ready. Size=" << size << " bytes";
	return msg.str();
}
