/**
 * @file packettest.cpp
 * @brief Manual usage and smoke tests for the @c packet and @c Request classes.
 * @author Sebastian Solorzano
 * @details
 * This file demonstrates and validates the core send/receive pipeline without
 * a real network socket. In production, the only difference would be replacing
 * the direct buffer hand-off with a @c send() / @c recv() pair over a TCP socket.
 * TCP guarantees delivery integrity, so no additional corruption checks are
 * needed at this layer beyond the CRC.
 *
 * Three scenarios are exercised:
 *  -# Basic @c Request construction and serialization.
 *  -# Raw payload wrapped in a @c packet and reconstructed on the receive side.
 *  -# A serialized @c Request carried inside a @c pkt_req packet, then unwrapped.
 *
 * ### Compile
 * @code
 * g++ packettest.cpp Packet.h Request.h -o <name>
 * @endcode
 *
 * ### Large-file chunking (not yet implemented)
 * The commented-out pseudocode at the bottom of @c main() sketches a chunked
 * send loop for payloads larger than @c MAX_PKTSIZE. A recommended extension
 * would be sending a leading @c pkt_dat packet containing the filename and
 * total file size so the receiver can track transfer progress.
 */

#include <stdio.h>
#include <stdlib.h>
#include "Packet.h"
#include "Request.h"

/**
 * @brief Smoke-tests @c Request and @c packet construction, serialization,
 *        and reconstruction without a live socket.
 *
 * ### Test 1 — Request round-trip
 * Constructs a @c Request with an arbitrary type and body, serializes it, and
 * prints the fields to confirm the values survive serialization.
 *
 * ### Test 2 — Packet round-trip (raw payload)
 * Wraps a plain string in a @c packet using @c pkt_emgcy (chosen to exercise
 * negative/high enum values). Serializes to a wire buffer, reconstructs a
 * receive-side @c packet from that buffer, and prints all header fields plus
 * the payload to verify round-trip fidelity.
 *
 * ### Test 3 — Request carried inside a packet
 * Serializes a @c Request, stuffs it into a @c pkt_req packet, serializes the
 * packet, reconstructs both the packet and the inner @c Request on the receive
 * side, and prints all fields. This is the canonical pattern for real client
 * messages.
 *
 * @note In real use, replace the direct buffer hand-off between @c Serialize()
 *       and the receiving @c packet constructor with @c send() / @c recv().
 *       TCP ensures the bytes arrive intact, so no extra integrity step is
 *       needed beyond confirming the @c transmit_flag.
 *
 * @note After receiving, read the packet's type flag with @c getPKType() and
 *       branch on the @c PKTYPE value — a few @c if statements are sufficient
 *       to dispatch to the correct handler.
 *
 * @return Always returns 0.
 */
int main(void) {

    // -------------------------------------------------------------------------
    // Test 1: Request construction and serialization
    // -------------------------------------------------------------------------
    printf("proof that request works:\n");

    char hel[] = "hes nskdfs d fs";
    Request q = Request(78, sizeof(hel), hel);
    int placeholder;
    printf("q: %d,%d,%s\nraw:%s\n", q.getType(), q.getBsize(), q.getBody(), q.serializeRequest(&placeholder));

    // -------------------------------------------------------------------------
    // Test 2: Packet round-trip with a raw string payload
    // pkt_emgcy was deliberately chosen to verify that high enum values
    // serialize and reconstruct without sign/truncation issues.
    // ------------------------------------------------------------------------
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

    // -------------------------------------------------------------------------
    // Test 3: Request serialized and carried inside a pkt_req packet
    // This mirrors the pattern used by every real client command.
    // -------------------------------------------------------------------------
    printf("proof that send request through packet:\n");

    packet reqpktsend = packet();

    char reqmsg[] = "this is a fancy request message asking for weather or somethin idk man";
    Request sss= Request(TEST, sizeof(reqmsg), reqmsg);
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


    // -------------------------------------------------------------------------
    // TODO: Large-file chunked send (not yet implemented)
    //
    // Sketch of a chunked send loop for payloads > MAX_PKTSIZE:
    //
    //   packet big = packet();
    //   // char bigbuf[REALLY_BIG] = ...; or load via ifstream, etc.
    //
    //   int sent = big.PopulPacket(bigbuf, totalSize, id, pkt_dat);
    //   while (sent < totalSize) {
    //       char* wire = big.Serialize(&wireSize);
    //       send(sockfd, wire, wireSize, 0);
    //       sent += big.PopulPacket(bigbuf + sent, totalSize - sent, id, pkt_dat);
    //   }
    //
    // Recommended: send a leading pkt_dat packet first, containing the filename
    // and total byte count, so the receiver can track how much has arrived.
    // -------------------------------------------------------------------------

    return 0;
}
