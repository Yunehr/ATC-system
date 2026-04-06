/**
 * @file WeatherService.cpp
 * @brief Implementation of the WeatherService class.
 *
 * Provides weather data lookups against @c weather.csv, a two-column CSV
 * mapping ICAO airport codes to weather condition strings.
 *
 * @par weather.csv format
 * @code
 * AirportID, Conditions
 * YYZ, Clear skies, visibility 10 SM, wind 270 at 12 kt
 * @endcode
 */
#include "WeatherService.hpp"
#include <fstream>
#include <string>
#include <sstream>
 
// ---------------------------------------------------------
// File helpers
// ---------------------------------------------------------
 
/**
 * @brief Opens a data file by probing two candidate directories.
 *
 * Tries @c ../data/<fileName> first, then falls back to @c data/<fileName>.
 *
 * @param fileName Bare filename to open (e.g. @c "weather.csv").
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
// WeatherService implementation
// ---------------------------------------------------------
 
/**
 * @brief Looks up weather conditions for the specified airport in @c weather.csv.
 *
 * Performs a linear search of the CSV for a row whose @c AirportID column
 * matches @p location (case-sensitive).  The header row is skipped.
 *
 * @param location ICAO airport code or location identifier to look up
 *                 (e.g. @c "YYZ").
 * @return @c "<AirportID>: <conditions>" on success, or one of:
 *         - @c "weather.csv not found" if the file could not be opened.
 *         - @c "No weather found for: <location>" if no matching row exists.
 */
std::string WeatherService::getWeather(const std::string& location) {
    std::ifstream in = openDataFile("weather.csv");

    if (!in.is_open()) {
        return "weather.csv not found";
    }

    std::string line;

    // skip header line
    std::getline(in, line);

    while (std::getline(in, line)) {
        std::stringstream ss(line);

        std::string airportID;
        std::string conditions;

        if (std::getline(ss, airportID, ',') && std::getline(ss, conditions)) {
            if (airportID == location) {
                return airportID + ": " + conditions;
            }
        }
    }

    return "No weather found for: " + location;
}