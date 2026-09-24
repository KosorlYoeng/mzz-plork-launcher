#pragma once
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "Json.h"
#include "ServerConnection.h"

namespace mzzplork {

// Implements the client side of server/src/protocol.js purely against
// the abstract IServerConnection interface -- works with any
// implementation (the real TcpServerConnection, or a fake in a test)
// without knowing anything about sockets.
//
// The wire protocol is asynchronous (messages arrive on whatever thread
// the connection delivers them on), but the client's state machine
// (CONNECTING -> AUTHENTICATING -> DOWNLOADING_RESOURCES) is sequential,
// so this exposes blocking request/response helpers: send a request,
// wait (with a timeout) for the matching reply, backed by a condition
// variable rather than polling.
class ProtocolClient {
public:
    explicit ProtocolClient(IServerConnection& connection);

    // Registers this instance as the connection's message handler. Call
    // once, after Connect() succeeds and before sending anything.
    void Start();

    bool SendHello(const std::string& clientProtocolVersion, json::Object& outAck, int timeoutMs = 5000);
    bool SendAuth(const std::string& clientId, const std::string& displayName, json::Object& outResult, int timeoutMs = 5000);
    bool SendHeartbeat(int timeoutMs = 5000);
    bool RequestResourceManifest(json::Object& outManifest, int timeoutMs = 10000);
    // Notifies the server that resource loading finished (docs/resources.md's
    // client flow: ... -> Load resources -> Notify server -> Enter ready
    // state). Waits for the server's acknowledgment.
    bool SendResourcesReady(int loadedCount, int failedCount, int timeoutMs = 5000);
    // Fire-and-forget: no reply expected, like a real game's frequent
    // position updates -- the next update supersedes this one regardless,
    // so waiting for an ack would only add latency for no benefit.
    bool SendPlayerState(const std::string& payloadJson);
    void SendDisconnect(const std::string& reason);

    // Unsolicited messages: "event" (server-pushed events) and "kick"
    // (server-initiated disconnect) don't fit the request/response
    // pattern above, so they're delivered via callbacks instead.
    void SetEventSink(std::function<void(const std::string& eventType, const json::Object& data)> sink);
    void SetKickHandler(std::function<void(const std::string& reason)> handler);

private:
    bool RequestReply(const std::string& requestType, const std::string& payloadJson, const std::string& expectedReplyType, json::Object& outPayload, int timeoutMs);
    void OnMessage(const ServerMessage& message);

    IServerConnection& connection_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, std::string> pendingReplies_;  // reply type -> raw JSON payload, consumed by RequestReply
    std::function<void(const std::string&, const json::Object&)> eventSink_;
    std::function<void(const std::string&)> kickHandler_;
};

}


