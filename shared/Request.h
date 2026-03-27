#pragma once
#include <cstdint>
#include <cstring>

typedef enum reqtyp{
    req_auth = 0,      // For login
    req_weather = 1,   // For weather.csv
    req_telemetry = 2, // For coordinates
    req_file = 3,      // For 1MB FlightManual.pdf 
    req_emergency = 4, // To trigger state change 
    req_test = 100
} REQTYPE;

class Request {
    uint8_t type;
    int32_t bodysize;
    char* body;

public:
    Request(uint8_t t, int32_t bodsize, char* bod) {
        type = t;
        bodysize = bodsize;

        body = new char[bodsize];
        memcpy(body, bod, bodsize);
    }

    // Constructor for receiving side
    Request(char* raw, int32_t size) {
        type = (uint8_t)raw[0];
        bodysize = size - 1;
        body = new char[bodysize];
        memcpy(body, raw + 1, bodysize);
    }

    // DESTRUCTOR: Prevents the memory leaks that would occur during 1MB file transfer
    ~Request() {
        if (body) delete[] body;
    }

    // Deep Copy Assignment (Essential for passing requests to handlers)
    Request& operator=(const Request& other) {
        if (this == &other) return *this;
        if (body) delete[] body;
        type = other.type;
        bodysize = other.bodysize;
        body = new char[bodysize];
        memcpy(body, other.body, bodysize);
        return *this;
    }

    char* serializeRequest(int32_t* finalsize) {
        // Use a persistent buffer, or handle deallocation in the caller
        char* serial = new char[1 + bodysize];
        serial[0] = (char)type;
        memcpy(serial + 1, body, bodysize);
        if (finalsize) *finalsize = 1 + bodysize;
        return serial;
    }
    uint8_t getType() { return type; }
    int32_t getBsize() { return bodysize; }
    char* getBody() { return body; }
};