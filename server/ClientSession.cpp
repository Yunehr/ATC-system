/**
 * @file ClientSession.cpp
 * @brief Implementation of the ClientSession class.
 *
 * Handles pilot authentication by parsing a comma-separated credential
 * string and validating it against @c LoginAuth.csv.  The data file is
 * located using the same two-path probe strategy used elsewhere in the
 * client (@c ../data/ first, then @c data/).
 */
 
#include "ClientSession.hpp"
#include <fstream>
#include <sstream>

 
// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Opens a data file by probing two candidate directories.
 *
 * Tries @c ../data/<fileName> first (standard layout relative to the
 * executable), then falls back to @c data/<fileName> (e.g. when running
 * directly from the build output directory).
 *
 * @param fileName Bare filename to open (e.g. @c "LoginAuth.csv").
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
// ClientSession implementation
// ---------------------------------------------------------
 
/**
 * @brief Splits a @c "username,password" credential string into its components.
 *
 * Expects exactly one comma delimiter.  Fields may be empty strings, but
 * both the username and password tokens must be present for the parse to
 * succeed.
 *
 * @param credentials Combined credential string in @c "username,password" format.
 * @param username    Populated with the substring before the first comma.
 * @param password    Populated with the substring after the first comma.
 * @return @c true  if both tokens were extracted successfully.
 * @return @c false if the string contains no comma or is otherwise malformed.
 */
bool ClientSession::parseCredentials(const std::string& credentials, std::string& username, std::string& password) {
	std::stringstream ss(credentials);
	return (std::getline(ss, username, ',') && std::getline(ss, password));
}

/**
 * @brief Authenticates a pilot against the credentials stored in @c LoginAuth.csv.
 *
 * Parses @p credentials into a username/password pair, then performs a
 * linear search of @c LoginAuth.csv for a matching row.  The CSV is expected
 * to have the following column layout (with a header row that is skipped):
 * @code
 * Username, Password, LicenseNumber
 * @endcode
 *
 * On a successful match the pilot's license number from the CSV is included
 * in the response string.
 *
 * @param credentials Combined credential string in @c "username,password" format,
 *                    as received from the packet payload.
 * @return One of the following response strings:
 *         - @c "Authentication Successful (License: <license>)" on success.
 *         - @c "Authentication Failed: expected Username,Password" if the
 *           credential string could not be parsed.
 *         - @c "Authentication Failed: LoginAuth.csv not found" if the data
 *           file could not be opened.
 *         - @c "Authentication Failed" if no matching record was found.
 */
std::string ClientSession::authenticate(const std::string& credentials) {
	std::string username;
	std::string password;
	if (!parseCredentials(credentials, username, password)) {
		return "Authentication Failed: expected Username,Password";
	}

	std::ifstream in = openDataFile("LoginAuth.csv");
	if (!in.is_open()) {
		return "Authentication Failed: LoginAuth.csv not found";
	}

	std::string line;
	std::getline(in, line); // header

	while (std::getline(in, line)) {
		std::stringstream row(line);
		std::string fileUser;
		std::string filePass;
		std::string fileLicense;

		if (!std::getline(row, fileUser, ',')) continue;
		if (!std::getline(row, filePass, ',')) continue;
		if (!std::getline(row, fileLicense)) continue;

		if (fileUser == username && filePass == password) {
			return "Authentication Successful (License: " + fileLicense + ")";
		}
	}

	return "Authentication Failed";
}
