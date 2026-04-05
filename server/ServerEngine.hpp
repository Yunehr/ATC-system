/**
 * @file ServerEngine.hpp
 * @brief Declaration of the ServerEngine class.
 *
 * ServerEngine owns the TCP listen socket and drives the full server
 * lifecycle: binding, accepting, per-client packet dispatch, and shutdown.
 * Each accepted connection is handled synchronously (one client at a time)
 * by handleClient(), which owns its own StateMachine instance for fully
 * isolated session state.
 *
 * @par Structured stdout protocol
 * ServerEngine writes machine-readable prefixed lines to stdout that are
 * consumed by the Python ATCScreen dashboard:
 *
 * | Prefix                                          | Purpose                        |
 * |-------------------------------------------------|--------------------------------|
 * | @c "STATE: <name>"                              | Server state transition.       |
 * | @c "TRANSFER START: total=<N>"                  | File transfer starting.        |
 * | @c "TRANSFER PROGRESS: current=<C> total=<T>"  | Chunk sent.                    |
 * | @c "TRANSFER COMPLETE: current=<C> total=<T>"  | Transfer finished.             |
 * | @c "Client connected!"                          | New connection accepted.       |
 * | @c "Client disconnected."                       | Connection closed.             |
 */
 
#pragma once
#include <winsock2.h>

/**
 * @class ServerEngine
 * @brief Manages the TCP listen socket and per-client packet dispatch loop.
 *
 * Responsibilities:
 * - Creating, binding, and listening on the server TCP socket (start()).
 * - Accepting client connections sequentially in a blocking loop (run()).
 * - Dispatching incoming packets to authentication, request, and emergency
 *   handlers within handleClient().
 * - Streaming the flight manual PDF to a client in fixed-size chunks
 *   (sendFlightManual()).
 * - Cleanly closing the listen socket on stop() or destruction.
 *
 * @note Winsock initialisation (WSAStartup / WSACleanup) is the caller's
 *       responsibility and must be performed before constructing this object.
 */
class ServerEngine {
public:

    /**
     * @brief Constructs a ServerEngine with an invalid listen socket and stopped state.
     *
     * The socket is not opened until start() is called.
     */
    ServerEngine();
    /**
     * @brief Destructor — calls stop() to ensure the listen socket is closed.
     */
    ~ServerEngine();

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------
 
    /**
     * @brief Binds a TCP listen socket to the given port and begins listening.
     *
     * Creates an @c AF_INET/@c SOCK_STREAM socket, binds it to @c INADDR_ANY
     * on @p port, and calls listen().  Emits @c "STATE: STARTUP/AUTH" to
     * stdout on success so the ATCScreen dashboard can initialise its state
     * indicator.
     *
     * On any failure the socket is closed and reset to @c INVALID_SOCKET.
     *
     * @param port TCP port to listen on (e.g. @c 8080).
     * @return @c true  if the socket is bound and listening.
     * @return @c false if socket(), bind(), or listen() failed.
     */    
    bool start(unsigned short port);
    
    /**
     * @brief Blocks in the accept loop, handling one client connection at a time.
     *
     * For each accepted connection, calls handleClient() (blocking until the
     * client disconnects), then closes the client socket before accepting the
     * next one.  Failed accept() calls are logged and skipped rather than
     * treated as fatal.  Exits when @ref running is set to @c false by stop().
     */
    void run();               // accept loop
    /**
     * @brief Sets @ref running to @c false and closes the listen socket.
     *
     * Safe to call multiple times — the @c INVALID_SOCKET guard prevents
     * double-close.  Also called automatically by the destructor.
     */
    void stop();

    /**
     * @brief Streams the flight manual PDF to the client in fixed-size chunks.
     *
     * Each @c pkt_dat payload is prefixed with a 4-byte little-endian write
     * offset, matching the layout expected by FileReceiver::processPacket()
     * on the client side.  All chunks except the last carry
     * @c PKT_TRNSMT_INCOMP; the final chunk carries @c PKT_TRNSMT_COMP.
     *
     * Emits @c "TRANSFER START:", @c "TRANSFER PROGRESS:", and
     * @c "TRANSFER COMPLETE:" lines to stdout for the ATCScreen dashboard.
     *
     * If the file opens but contains zero bytes, a single zero-length
     * @c pkt_dat packet with @c PKT_TRNSMT_COMP is sent so the client can
     * exit its receive loop cleanly.
     *
     * @param clientSock The connected client socket to write chunks to.
     * @param clientId   Client ID stamped into every outgoing packet header.
     * @return @c true  if all chunks were sent successfully.
     * @return @c false if the file could not be opened or any send failed.
     *
     * @note This method is public to facilitate unit testing; it is normally
     *       called only from handleClient().
     */
    bool sendFlightManual(SOCKET clientSock, unsigned char clientId);
private:
    /// @brief The TCP listen socket, or @c INVALID_SOCKET when not listening.
    SOCKET listenSock;
    
    /// @brief @c true while the server is running; set to @c false by stop().
    bool running;

    /**
     * @brief Receives and dispatches packets for a single connected client.
     *
     * Runs a blocking receive loop until the client disconnects or a fatal
     * send error occurs.  Dispatches by packet type:
     * - @c pkt_auth  — ClientSession::authenticate(); StateMachine::onAuthSuccess().
     * - @c pkt_emgcy — StateMachine::onEmergency(); sends acknowledgement.
     * - @c pkt_req   — decoded to @c reqtyp and forwarded to the appropriate
     *                  service (WeatherService, FileTransferManager, etc.).
     * - Other        — sends a @c pkt_empty error response.
     *
     * All received and transmitted packets are logged via Logger::logPacket()
     * (REQ-LOG-010/030/040).  Each session owns its own StateMachine instance,
     * so connection state is fully isolated.
     *
     * @param clientSock The accepted client socket for this session.
     */
    void handleClient(SOCKET clientSock);
    
};