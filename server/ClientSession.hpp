/**
 * @file ClientSession.hpp
 * @brief Declaration of the ClientSession class.
 *
 * Provides static pilot authentication against credentials stored in
 * @c LoginAuth.csv.  ClientSession is a stateless utility class and is
 * not intended to be instantiated.
 */
 
#pragma once
#include <string>

/**
 * @class ClientSession
 * @brief Static utility class for pilot authentication.
 *
 * Validates a @c "username,password" credential string against
 * @c LoginAuth.csv and returns a structured response string consumed
 * by the packet handler in ClientApp.
 *
 * All state is held in the CSV file; no instance state is required.
 */
class ClientSession {
public:
    /**
     * @brief Authenticates a pilot against the credentials in @c LoginAuth.csv.
     *
     * Delegates parsing to parseCredentials(), then performs a linear search
     * of the CSV for a matching username/password pair.
     *
     * @param credentials Combined credential string in @c "username,password" format.
     * @return One of the following response strings:
     *         - @c "Authentication Successful (License: <license>)" on success.
     *         - @c "Authentication Failed: expected Username,Password" if the
     *           credential string could not be parsed.
     *         - @c "Authentication Failed: LoginAuth.csv not found" if the data
     *           file could not be opened.
     *         - @c "Authentication Failed" if no matching record was found.
     */
	static std::string authenticate(const std::string& credentials);

private:
 
    /**
     * @brief Splits a @c "username,password" string into its two components.
     *
     * @param credentials Combined credential string to parse.
     * @param username    Populated with the substring before the first comma.
     * @param password    Populated with the substring after the first comma.
     * @return @c true  if both tokens were extracted successfully.
     * @return @c false if the string contains no comma or is otherwise malformed.
     */
	static bool parseCredentials(const std::string& credentials, std::string& username, std::string& password);
};
