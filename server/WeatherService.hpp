/**
 * @file WeatherService.hpp
 * @brief Declaration of the WeatherService class.
 *
 * Provides a single static method for looking up weather conditions from
 * @c weather.csv.  WeatherService is a stateless utility class and is not
 * intended to be instantiated.
 */
#pragma once
#include <string>
/**
 * @class WeatherService
 * @brief Static utility class for airport weather data lookups.
 *
 * Reads weather conditions from @c weather.csv, a two-column CSV mapping
 * ICAO airport codes to weather condition strings.  The file is located
 * using the standard two-path probe (@c ../data/ then @c data/).
 *
 * Called by ServerEngine::handleClient() in response to @c req_weather
 * requests.
 */
class WeatherService {
public:
 
/**
 * @brief Looks up weather conditions for the specified airport.
 *
 * Performs a case-sensitive linear search of @c weather.csv for a row
 * whose @c AirportID column matches @p location.
 *
 * @param location ICAO airport code or location identifier
 *                 (e.g. @c "YYZ").
 * @return @c "<AirportID>: <conditions>" on success, or one of:
 *         - @c "weather.csv not found" if the file could not be opened.
 *         - @c "No weather found for: <location>" if no matching row exists.
 */
    static std::string getWeather(const std::string& location);
};