#pragma once
//
// Client<->server network contract. Phase 1 left this an interface-only
// placeholder pending Phase 8/9; this phase implements it for real (see
// TcpServerConnection.h/.cpp) against the MzzPlork Server protocol
// (server/src/protocol.js) -- entirely independent of GTA V. Nothing
// here talks to the game; see GameIntegration.h for that boundary.
//
#include <functional>
#include <string>

namespace mzzplork {

struct ServerMessage {
    std::string type;
    std::string payload;  // JSON-encoded payload; see server/src/protocol.js for message shapes
};

class IServerConnection {
public:
    virtual ~IServerConnection() = default;

    virtual bool Connect(const std::string& host, unsigned short port) = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;

    // Sent only after Connect() succeeds; must go through whatever
    // authentication scheme the server defines, not around it.
    virtual bool SendHandshake() = 0;

    // Sends an arbitrary protocol message (type + JSON-encoded payload).
    // Used for everything beyond the initial handshake: auth, heartbeat,
    // resource manifest requests, disconnect notices.
    virtual bool Send(const std::string& type, const std::string& payloadJson) = 0;

    virtual void SetMessageHandler(std::function<void(const ServerMessage&)> handler) = 0;
};

}
