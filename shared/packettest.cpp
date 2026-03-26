//sebastian solorzano -- atc tower pr4j -- packet test
// testing / usage for the packet, basically 

#include <stdio.h>
#include <stdlib.h>
#include "Packet.h"

//okay I couldn't actually be bothered to do individual unit testing because ughh
//but anyways this shows usage and functionality of the packet.
//only difference in actual use would be send()ing serialpck and recv()ing it into recvp.
//you don't actually have to check that everything's okay on that front
//because the whole point of TCP is that it makes sure its all okay for you

//lastly I don't *think* making functions to send and recieve is my job (or at least for the moment) so I made no such functions
//basically after you recieve your packet just read the flags you get and decide what to do from there. a few ifs should be sufficient, prob

int main(void) {


    printf("proof that request works:\n");

    char hel[] = "hes nskdfs d fs";
    Request q = Request(78, sizeof(hel), hel);
    int placeholder;
    printf("q: %d,%d,%s\nraw:%s\n", q.getType(), q.getBsize(), q.getBody(), q.serializeRequest(&placeholder));


    printf("proof that packet works:\n");

    packet sendp = packet();

    char tests[] = "hellohello this is a test (ignore that its a srting pretend it isn't)";
    int userid = 0x0a;//entirely arbitrary, just keep it <16
    int size1 = sendp.PopulPacket(tests, sizeof(tests), userid, pkt_emgcy); //emgcy was to test -nums, they all work

    int size2;
    char* serialpck = sendp.Serialize(&size2);
    //size2 would be sent as size through the send()

    packet recvp = packet(serialpck);
    char flag = recvp.getTFlag();
    char id = recvp.getCLID();
    char type = recvp.getPKType();
    char* dat = recvp.getData();
    int size = recvp.getPloadLength();
    printf("transmssion flag: %d\nclient id: %d\npacket type: %d\ndata: %s\nsize: %d\n", flag, id, type, dat, size);


    printf("proof that send request through packet:\n");

    packet reqpktsend = packet();

    char reqmsg[] = "this is a fancy request message asking for weather or somethin idk man";
    Request sss= Request(99, sizeof(reqmsg), reqmsg);
    printf("before sending: %d, %d, %s\n", sss.getType(), sss.getBsize(), sss.getBody());

    int reqpktID = 0x0a;

    int reqsize;
    char* serialRequest = sss.serializeRequest(&reqsize);
    int reqpktfillsize = reqpktsend.PopulPacket(serialRequest, reqsize, reqpktID, pkt_req);

    int reqpktSerialsize;
    char* reqpktSerialized = reqpktsend.Serialize(&reqpktSerialsize);

    packet reqpktrecv = packet(reqpktSerialized);
    char reqpktflag = reqpktrecv.getTFlag();
    char reqpktIDrecv = reqpktrecv.getCLID();
    char reqpktType = reqpktrecv.getPKType();
    Request rrr = Request(reqpktrecv.getData(), reqpktrecv.getPloadLength());

    printf("transmssion flag: %d\nclient id: %d\npacket type: %d\ndata: %s\nsize: %d\n", reqpktflag, reqpktIDrecv, reqpktType, reqpktrecv.getData(), reqpktrecv.getPloadLength());

    printf("request type: %d\nrequest body: %s\n", rrr.getType(), rrr.getBody());




    //okay so i'm not actually writing the function for this (at least for the moment),
    //but here's an idea of how it could be done?
    /*
    packet big = packet();
    char big[really_big] = <whatever>; or ifstream x = however these work; or whatever else

    int size1 = sendp.PopulPacket(big, ...);
    while(size1 < sizeof(big)){
    serialize
    send(serial)

    size1 += sendp.populpacket(big +size1, ...);
    }
    */
    //i'm fairly certain this would work.
    //it might also be a good idea to send a dat packet first, with the filename and total size,
    //so the recieving end can calculate how much of the file they've recieved

}
