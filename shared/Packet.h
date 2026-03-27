#pragma once
#include <cstdint>  // For uint8_t and int32_t consistency
#include <cstring>   // REQUIRED for memset and memcpy 

#define EMTPY_PKTSIZE  6
#define MAX_PKTSIZE 250 
#define PKT_TRNSMT_COMP 0X0
#define PKT_TRNSMT_INCOMP 0x1

// --- LAYER 1: THE ENVELOPE (PKTYPE) ---
// Used by the Networking layer to route raw data
typedef enum pkTyFl {
    pkt_empty = 0x00,
    pkt_req   = 0x01,  // "There is a Request object inside this packet"
    pkt_dat   = 0x02,  // "This is raw file data for the 1MB transfer"
    pkt_auth  = 0x03,  // "This is an authentication attempt" 
    pkt_emgcy = 0x04   // "PRIORITY: Emergency state change"
} PKTYPE;

// --- LAYER 2: THE COMMAND (REQTYPE) ---
// Used by the Server State Machine to execute logic
typedef enum reqtyp {
    req_weather   = 0, // Pull from weather.csv
    req_telemetry = 1, // Log to System TrafficLog.csv 
    req_file      = 2, // Start 1MB FlightManual.pdf stream 
    req_taxi      = 3, // Request runway clearance 
    req_test      = 100
} REQTYPE;

class packet {

private:

    struct header {
        uint8_t transmit_flag : 1;
        uint8_t packet_type : 3;  //allows 7 flags
        uint8_t client_id : 4; //allows 15 client ids
        uint8_t payload_length;
       
    }HEAD;

    char* data;
    char* txbuff;
    int32_t CRC; //4 byte tail

public:
    //packet [creation side]
    packet() {
        data = nullptr;
        txbuff = nullptr;
        memset(&HEAD, 0, sizeof(header));
    }
    
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

    //Destructor: Prevents memory leaks during 1MB tranfer
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

    //packet [recieving side]
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

    //fills the packet. returns amount it consumed if data sent is too big
    int PopulPacket(char* datsrc, int size, char id, char type) {
        int consumed;

        if (data) {
            delete[] data;
        }

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

    char getTFlag() { return HEAD.transmit_flag; }
    char getPKType() { return HEAD.packet_type; }
    char getCLID() { return HEAD.client_id; }
    unsigned char getPloadLength() { return HEAD.payload_length; }
    char* getData() { return data; }
    int GetCRC() { return CRC; }

};