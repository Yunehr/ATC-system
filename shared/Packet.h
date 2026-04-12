/**
 * @file packet.h
 * @brief Defines the packet framing layer for all client-server communication.
 *
 * A packet is the lowest-level transmission unit in the system. It wraps any
 * payload (requests, file data, auth, emergency signals) in a fixed header and
 * a 4-byte CRC tail, producing a flat byte buffer suitable for wire transport.
 *
 * Packet layout on the wire:
 * @code
 * [ header (2 bytes) | payload (0–249 bytes) | CRC (4 bytes) ]
 * @endcode
 *
 * Requirements covered:
 * - REQ-SYS-020: Packet population and payload wrapping
 * - REQ-COM-010: Serialization for transport
 * - REQ-INT-040: Receiving and rebuilding from raw bytes
 */
#pragma once
#include <cstdint>  // For uint8_t and int32_t consistency
#include <cstring>   // REQUIRED for memset and memcpy 
/** @brief Size in bytes of a packet containing no payload (header + CRC). */
#define EMTPY_PKTSIZE  6
/** @brief Maximum total packet size in bytes, including header and payload. */
#define MAX_PKTSIZE 250 
/** @brief Transmit flag value indicating the full payload fit in one packet. */
#define PKT_TRNSMT_COMP 0X0
/** @brief Transmit flag value indicating the payload was truncated and more packets follow. */
#define PKT_TRNSMT_INCOMP 0x1

/**
 * @brief Networking-layer envelope type, used to route a packet to the correct handler.
 *
 * This is the outer classification of a packet — it tells the networking layer
 * *what kind of data* is inside, before any parsing of the body occurs.
 */
typedef enum pkTyFl {
    pkt_empty = 0x00, ///< No payload; used for initialisation or keep-alive.
    pkt_req   = 0x01,  ///< Payload is a serialized @c Request object.
    pkt_dat   = 0x02,   ///< Payload is raw file data (e.g. 1 MB FlightManual.pdf chunk).
    pkt_auth  = 0x03,  ///< Payload is an authentication attempt.
    pkt_emgcy = 0x04   ///< Priority packet: emergency state change signal.
} PKTYPE;



/**
 * @brief Server state machine command type, carried inside a @c pkt_req packet.
 *
 * Once the networking layer confirms a packet is of type @c pkt_req, the server
 * state machine reads this value from the unwrapped @c Request to decide which
 * action to execute.
 */
typedef enum reqtyp {
    req_weather   = 0, ///< Fetch current weather data from @c weather.csv.
    req_telemetry = 1, ///< Log a telemetry entry to @c TrafficLog.csv.
    req_file      = 2, ///< Initiate a 1 MB @c FlightManual.pdf stream.
    req_taxi      = 3, ///< Request runway clearance from ATC.
    req_fplan     = 4, ///< Fetch flight plan data from @c Flight_plan.csv.
    req_traffic   = 5, ///< Request current traffic information.
    req_resolve   = 6, ///< Resolve or clear an active emergency state.
    req_test      = 100 ///< Reserved for testing and integration QA.
} REQTYPE;

/**
 * @brief A framed, serializable network packet with header, payload, and CRC.
 *
 * The @c packet class handles both the transmit path (populate → serialize) and
 * the receive path (construct from raw bytes → extract payload). It owns its
 * internal buffers and manages their lifetime through RAII.
 *
 * ### Transmit path
 * @code
 * packet pkt;
 * pkt.PopulPacket(src, size, clientId, pkt_req);
 * int len;
 * char* wire = pkt.Serialize(&len);  // wire is valid for pkt's lifetime
 * @endcode
 *
 * ### Receive path
 * @code
 * packet pkt(wireBuffer);            // parses header, body, and CRC
 * char* body = pkt.getData();
 * @endcode
 *
 * @warning The buffer returned by @c Serialize() is owned by the packet.
 *          Do not @c delete it externally, and do not use it after the
 *          packet is destroyed or @c Serialize() is called again.
 */
class packet {

private:
    /**
     * @brief Fixed 2-byte packet header, packed into a bit-field struct.
     *
     * Field layout (LSB → MSB within each byte):
     * - Byte 0: @c transmit_flag (1 bit) | @c packet_type (3 bits) | @c client_id (4 bits)
     * - Byte 1: @c payload_length (8 bits, max 249)
     */
    struct header {
        uint8_t transmit_flag : 1; ///< 0 = complete, 1 = more fragments follow.
        uint8_t packet_type : 3;   ///< @c PKTYPE value; supports up to 7 distinct types.
        uint8_t client_id : 4;     ///< Sender client ID; supports up to 15 clients.
        uint8_t payload_length;    ///< Number of payload bytes that follow the header.
       
    }HEAD;

    char* data; ///< Heap-allocated payload buffer. Owned by this object.
    char* txbuff; ///< Heap-allocated serialized wire buffer. Owned by this object.
    int32_t CRC; ///< 4-byte checksum appended as the packet tail after serialization.

public:
     /**
     * @brief Default constructor. Creates an empty packet ready to be populated.
     *
     * All header fields are zeroed; @c data and @c txbuff are set to @c nullptr.
     */
    packet() {
        data = nullptr;
        txbuff = nullptr;
        memset(&HEAD, 0, sizeof(header));
    }
        /**
     * @brief Copy assignment operator. Performs a deep copy of payload data.
     *
     * Frees any existing buffers, copies the header and CRC, then allocates a
     * new buffer and copies the payload bytes. The @c txbuff is not copied —
     * call @c Serialize() on the new instance if a wire buffer is needed.
     *
     * @param other The source packet to copy from.
     * @return A reference to this packet.
     */
    packet& operator=(const packet& other) {
        if (this == &other) return *this;
        
        // 1. Clean up our existing memory to prevent leaks
        if (data) delete[] data;
        if (txbuff) delete[] txbuff;

        // 2. Copy the Header and CRC
        HEAD = other.HEAD;
        CRC = other.CRC;
        txbuff = nullptr; // Don't copy the transmission buffer

        // 3. Perform a DEEP COPY of the data 
        if (other.data) {
            data = new char[HEAD.payload_length];
            memcpy(data, other.data, HEAD.payload_length);
        } else {
            data = nullptr;
        }
        return *this;
    }

        /**
     * @brief Destructor. Frees the payload and wire buffers.
     *
     * Both @c data and @c txbuff are deleted if non-null. Designed to prevent
     * memory leaks during large multi-packet transfers (e.g. the 1 MB file stream).
     */
    ~packet(){
        if (data) {
            delete[] data;
            data = nullptr;
        }
        if (txbuff) {
            delete[] txbuff;
            txbuff = nullptr;
        }
    }

    /**
     * @brief Receive-side constructor. Parses a raw wire buffer into a packet.
     *
     * Copies the header, allocates a new payload buffer and copies the body,
     * then reads the CRC tail. The original @p pakraw buffer is not retained.
     *
     * @param pakraw Pointer to a contiguous byte buffer in the wire format:
     *               @c [header | payload | CRC].
     *
     * @warning @p pakraw must be at least @c sizeof(header) + @c payload_length
     *          + @c sizeof(CRC) bytes long. No bounds checking is performed.
     */
    packet(char* pakraw) {
        //head
        memcpy(&HEAD, pakraw, sizeof(header));
        //body
        data = new char[HEAD.payload_length];
        memcpy(data, pakraw + sizeof(header), HEAD.payload_length); //char*, no need for &
        //tail
        memcpy(&CRC, pakraw + sizeof(header) + HEAD.payload_length, sizeof(CRC));

        txbuff = nullptr;//we don't care about this guy
    }

        /**
     * @brief Populates the packet with a payload, truncating if necessary.
     *
     * Copies up to @c MAX_PKTSIZE bytes from @p datsrc. If @p size exceeds
     * @c MAX_PKTSIZE, the payload is truncated and @c transmit_flag is set to
     * @c PKT_TRNSMT_INCOMP to signal that more packets follow. Any previously
     * held payload buffer is freed before the new one is allocated.
     *
     * @param datsrc Pointer to the source data to copy into the packet body.
     * @param size   Number of bytes to copy from @p datsrc.
     * @param id     Client ID to embed in the header (0–15).
     * @param type   Packet type to embed in the header; see @c PKTYPE.
     * @return The number of bytes actually consumed from @p datsrc. If less
     *         than @p size, the caller is responsible for sending the remainder
     *         in subsequent packets.
     */
    int PopulPacket(char* datsrc, int size, char id, char type) {
        int consumed;

        if (data) {
            delete[] data;
        }

        HEAD.transmit_flag = PKT_TRNSMT_COMP;

        if (size >= MAX_PKTSIZE) {
            consumed = MAX_PKTSIZE;
            HEAD.transmit_flag = PKT_TRNSMT_INCOMP;
            //is it appropriate to modify that here? I think so
        }
        else
            consumed = size;

        //assigning data
        data = new char[consumed];
        memcpy(data, datsrc, consumed);

        //assigning head stuff
        HEAD.payload_length = consumed;
        HEAD.client_id = id;
        HEAD.packet_type = type;

        return consumed;
    }
        /**
     * @brief Serializes the packet into a flat wire buffer.
     *
     * Lays out @c [header | payload | CRC] contiguously in an internally
     * managed buffer. The CRC is computed over the header and payload bytes
     * immediately before writing. Any previously held @c txbuff is freed first.
     *
     * @param[out] finalsize Set to the total number of bytes written to the
     *                       returned buffer.
     * @return Pointer to the internal wire buffer. Valid until the next call
     *         to @c Serialize() or until the packet is destroyed.
     *
     * @warning Do not @c delete the returned pointer. See class-level warning.
     */

    char* Serialize(int* finalsize) {
        if (txbuff) {
            delete[] txbuff;
        }

        unsigned int fullsize = EMTPY_PKTSIZE + HEAD.payload_length;
        //not sure about it being unsigned here but whater. (should I make it size_t lmao)

        txbuff = new char[fullsize];

        memcpy(txbuff, &HEAD, sizeof(header));
        memcpy(txbuff + sizeof(header), data, HEAD.payload_length);

        CRC = calcCRC();
        memcpy(txbuff + sizeof(header) + HEAD.payload_length, &CRC, sizeof(CRC));

        //we have to return txbuff because its private, 
        //which means size has to be sent through a side effect
        *finalsize = fullsize;
        return txbuff;
    }
   /**
     * @brief Computes the packet's checksum over all header and payload bytes.
     *
     * Sums every byte of the header struct followed by every byte of the
     * payload, treating all bytes as unsigned. This value is written into the
     * CRC tail by @c Serialize() and can be compared between a transmitted and
     * received packet to detect corruption.
     *
     * @return The computed checksum as a signed 32-bit integer.
     *
     * @note This is a simple byte-sum checksum, not a standard CRC polynomial.
     *       It is sufficient for detecting single-byte errors but not burst errors.
     */
    int calcCRC() {
        int sum = 0;

        //sum every byte in the header
        uint8_t* hPtr = reinterpret_cast<uint8_t*>(&HEAD);
        for (size_t i = 0; i < sizeof(header); ++i) {
            sum += hPtr[i];
        }
        //sum every byte in the body
        if ( data != nullptr) {
            for (size_t i = 0; i < HEAD.payload_length; ++i) {
                sum += static_cast<uint8_t>(data[i]);
            }
        }
        return sum;
    }
    /** @brief Returns the transmit flag (0 = complete, 1 = more fragments). */
    char getTFlag() { return HEAD.transmit_flag; }
     /** @brief Returns the packet type field; see @c PKTYPE. */
    char getPKType() { return HEAD.packet_type; }
    /** @brief Returns the client ID embedded in the header. */
    char getCLID() { return HEAD.client_id; }
    /** @brief Returns the payload length in bytes as stored in the header. */
    unsigned char getPloadLength() { return HEAD.payload_length; }
    /** @brief Returns a pointer to the internal payload buffer. Not null-terminated. */
    char* getData() { return data; }
    /** @brief Returns the CRC value last written by @c Serialize(). */
    int GetCRC() { return CRC; }
     /**
     * @brief Sets the transmit flag based on a boolean-style input.
     *
     * @param flag Any non-zero value sets @c PKT_TRNSMT_INCOMP (more fragments);
     *             zero sets @c PKT_TRNSMT_COMP (complete).
     */
    void setTransmitFlag(char flag) { HEAD.transmit_flag = (flag ? PKT_TRNSMT_INCOMP : PKT_TRNSMT_COMP); }

};