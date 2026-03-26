//sebastian solorzano -- atc tower pr4j -- packet and networks 
//would it be easier to simply treat this as a header library? I'm not sure
//anyways. definition/implementation for Requests
//still protocol

#include <memory>
#include <cstring>

//OK GUYS (LIBAN, RYAN) FILL THIS ENUM WITH WHAT YOU NEED!
//                V
typedef enum reqtyp{
    weather,
    telemetry
}REQTYPE;

//okay so to my chagrin I did end up having to make a class, but honestly it's really simple
//once serialized its literally just : char, payload
//and the functions here will take care of everything for you
class Request {
    char type;
    int bodysize;
    char* body;
public:
    Request(char t, int bodsize, char* bod) {
        type = t;
        bodysize = bodsize;

        body = new char[bodsize];
        memcpy(body, bod, bodsize);
    }
    Request(char* raw, int size) {
        memcpy(&type, raw, sizeof(type));
        bodysize = size - sizeof(type);

        body = new char[bodysize];
        memcpy(body, raw + sizeof(type), bodysize);

    }
    char* serializeRequest(int* finalsize) {
        char* serial = new char[sizeof(type) + bodysize];
        memcpy(serial, &type, sizeof(type));
        memcpy(serial + sizeof(type), body, bodysize);

        *finalsize = sizeof(type) + bodysize;
        return serial;
        //there is a teeney tiny itty bitty tiny little chance that this may forget to deallocate things
        //cpp is a smart guy i'm sure he can handle it
    }
    //I decided not to include bodysize in the serial, because I know that packet already has that info regarding its payload
    //so we can just let that do the work for us
    //this keeps the whole thing smaller too, as an int is 4 whole bytes
    char getType() { return type; }
    int getBsize() { return bodysize; }
    char* getBody() { return body; }
    //can't be modified after creation, but I mean why would you
};