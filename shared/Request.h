/**
 * @file Request.h
 * @brief Defines the @c Request class: the application-layer command unit
 *        carried inside a @c pkt_req packet.
 *
 * A @c Request is the innermost payload of the communication stack. It pairs
 * a @c REQTYPE command byte with an arbitrary body buffer, and can be
 * serialized to / deserialized from a flat byte array for embedding inside a
 * @c packet.
 *
 * ### Wire format (serialized)
 * @code
 * [ type (1 byte) | body (bodysize bytes) ]
 * @endcode
 *
 * ### Typical usage
 * @code
 * // Send side
 * Request req(req_weather, dataLen, dataPtr);
 * int32_t serialSize;
 * char* serial = req.serializeRequest(&serialSize);
 * txPacket.PopulPacket(serial, serialSize, clientId, pkt_req);
 * delete[] serial;
 *
 * // Receive side
 * Request req(rxPacket.getData(), rxPacket.getPloadLength());
 * handleRequest(req.getType(), req.getBody(), req.getBsize());
 * @endcode
 *
 * @see packet.h for the outer framing layer.
 * @see REQTYPE for valid command type values.
 */
#pragma once
#include <cstdint>
#include <cstring>
#include "packet.h"

/**
 * @brief An application-layer command consisting of a type byte and a body payload.
 *
 * Owns its body buffer and manages its lifetime through RAII. Supports both
 * the transmit path (construct from type + raw data → serialize) and the
 * receive path (construct from a flat byte buffer → inspect fields).
 *
 * @warning @c serializeRequest() allocates a new buffer on every call.
 *          The caller is responsible for @c delete[]ing the returned pointer.
 *
 * @warning There is no copy constructor defined. If a @c Request is copied
 *          by value (e.g. passed to a handler by value), the default
 *          shallow copy will cause a double-free. Pass by reference or
 *          pointer, or add a copy constructor matching @c operator=.
 */
class Request {
    uint8_t type; ///< Command type; see @c REQTYPE for valid values.
    int32_t bodysize; ///< Number of bytes in @c body.
    char* body; ///< Heap-allocated payload buffer. Owned by this object.


public:
    /**
     * @brief Transmit-side constructor. Copies @p bod into an internal buffer.
     *
     * @param t       Command type; should be a value from @c REQTYPE.
     * @param bodsize Number of bytes to copy from @p bod.
     * @param bod     Pointer to the source payload data.
     *
     * @warning @p bodsize must accurately reflect the number of valid bytes
     *          at @p bod. No bounds checking is performed.
     */
    Request(uint8_t t, int32_t bodsize, char* bod) {
        type = t;
        bodysize = bodsize;

        body = new char[bodsize];
        memcpy(body, bod, bodsize);
    }

       /**
     * @brief Receive-side constructor. Deserializes a @c Request from a raw buffer.
     *
     * Reads the type from byte 0, then copies the remaining @p size - 1
     * bytes into the body buffer. Matches the layout produced by
     * @c serializeRequest().
     *
     * @param raw  Pointer to a serialized request buffer in the format
     *             @c [type (1 byte) | body (size-1 bytes)].
     * @param size Total number of bytes in @p raw, including the type byte.
     *
     * @warning @p raw must be at least @p size bytes long.
     *          No bounds checking is performed.
     */
    Request(char* raw, int32_t size) {
        type = (uint8_t)raw[0];
        bodysize = size - 1;
        body = new char[bodysize];
        memcpy(body, raw + 1, bodysize);
    }

      /**
     * @brief Destructor. Frees the body buffer.
     *
     * Essential during large multi-packet transfers where many @c Request
     * objects may be created and destroyed in rapid succession.
     */
    ~Request() {
        if (body) delete[] body;
    }

        /**
     * @brief Deep copy assignment operator.
     *
     * Frees the existing body buffer, then allocates a new one and copies
     * the source body into it. Required for safely passing @c Request objects
     * into handler functions or containers that use assignment.
     *
     * @param other The source @c Request to copy from.
     * @return A reference to this @c Request.
     */
    Request& operator=(const Request& other) {
        if (this == &other) return *this;
        if (body) delete[] body;
        type = other.type;
        bodysize = other.bodysize;
        body = new char[bodysize];
        memcpy(body, other.body, bodysize);
        return *this;
    }
    /**
     * @brief Serializes this @c Request into a flat byte buffer.
     *
     * Allocates a new buffer of @c 1 + bodysize bytes, writes the type
     * byte at index 0, then copies the body. The resulting layout matches
     * what the receive-side constructor @c Request(char*, int32_t) expects.
     *
     * @param[out] finalsize Set to the total number of bytes in the returned
     *                       buffer (@c 1 + bodysize). May be @c nullptr if
     *                       the size is not needed.
     * @return Pointer to a heap-allocated serialized buffer. The caller
     *         is responsible for calling @c delete[] on this pointer.
     *
     * @warning Failing to @c delete[] the returned pointer is a memory leak.
     */
    char* serializeRequest(int32_t* finalsize) {
        // Use a persistent buffer, or handle deallocation in the caller
        char* serial = new char[1 + bodysize];
        serial[0] = (char)type;
        memcpy(serial + 1, body, bodysize);
        if (finalsize) *finalsize = 1 + bodysize;
        return serial;
    }
     /** @brief Returns the command type. See @c REQTYPE for valid values. */
    uint8_t getType() { return type; }
    /** @brief Returns the number of bytes in the body payload. */
    int32_t getBsize() { return bodysize; }
        /**
     * @brief Returns a pointer to the internal body buffer.
     *
     * @warning The returned pointer is not null-terminated unless the
     *          original payload included a null byte. Do not pass directly
     *          to functions expecting a C-string unless you are certain
     *          the content is null-terminated.
     */
    char* getBody() { return body; }
};