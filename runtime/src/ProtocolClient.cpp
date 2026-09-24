#include "ProtocolClient.h"

#include <chrono>

namespace mzzplork {

ProtocolClient::ProtocolClient(IServerConnection& connection) : connection_(connection) {}

void ProtocolClient::Start() {
    connection_.SetMessageHandler([this](const ServerMessage& message) { OnMessage(message); });
}

void ProtocolClient::OnMessage(const ServerMessage& message) {
    if (message.type == "event") {
        auto parsed = json::ParseObject(message.payload);
        if (parsed.ok && eventSink_) {
            auto typeIt = parsed.value.find("eventType");
            auto dataIt = parsed.value.find("data");
            std::string eventType = typeIt != parsed.value.end() ? typeIt->second.AsString() : "";
            json::Object data = dataIt != parsed.value.end() && dataIt->second.type == json::ValueType::Object
                ? dataIt->second.objectValue
                : json::Object{};
            eventSink_(eventType, data);
        }
        return;
    }
    if (message.type == "kick") {
        auto parsed = json::ParseObject(message.payload);
        std::string reason = parsed.ok ? parsed.value["reason"].AsString("disconnected by server") : "disconnected by server";
        if (kickHandler_) kickHandler_(reason);
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pendingReplies_[message.type] = message.payload;
    cv_.notify_all();
}

bool ProtocolClient::RequestReply(const std::string& requestType, const std::string& payloadJson, const std::string& expectedReplyType, json::Object& outPayload, int timeoutMs) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingReplies_.erase(expectedReplyType);
    }
    if (!connection_.Send(requestType, payloadJson)) return false;

    std::unique_lock<std::mutex> lock(mutex_);
    bool arrived = cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return pendingReplies_.count(expectedReplyType) > 0;
    });
    if (!arrived) return false;

    std::string raw = pendingReplies_[expectedReplyType];
    pendingReplies_.erase(expectedReplyType);
    lock.unlock();

    auto parsed = json::ParseObject(raw);
    if (!parsed.ok) return false;
    outPayload = parsed.value;
    return true;
}

bool ProtocolClient::SendHello(const std::string& clientProtocolVersion, json::Object& outAck, int timeoutMs) {
    json::Object payload;
    payload["protocolVersion"] = json::Value::MakeString(clientProtocolVersion);
    return RequestReply("hello", json::StringifyObject(payload), "hello_ack", outAck, timeoutMs);
}

bool ProtocolClient::SendAuth(const std::string& clientId, const std::string& displayName, json::Object& outResult, int timeoutMs) {
    json::Object payload;
    payload["clientId"] = json::Value::MakeString(clientId);
    payload["displayName"] = json::Value::MakeString(displayName);
    return RequestReply("auth", json::StringifyObject(payload), "auth_result", outResult, timeoutMs);
}

bool ProtocolClient::SendHeartbeat(int timeoutMs) {
    json::Object ignored;
    return RequestReply("heartbeat", "{}", "heartbeat_ack", ignored, timeoutMs);
}

bool ProtocolClient::RequestResourceManifest(json::Object& outManifest, int timeoutMs) {
    return RequestReply("resource_manifest_request", "{}", "resource_manifest", outManifest, timeoutMs);
}

bool ProtocolClient::SendResourcesReady(int loadedCount, int failedCount, int timeoutMs) {
    json::Object payload;
    payload["loadedCount"] = json::Value::MakeNumber(loadedCount);
    payload["failedCount"] = json::Value::MakeNumber(failedCount);
    json::Object ack;
    return RequestReply("resources_ready", json::StringifyObject(payload), "resources_ready_ack", ack, timeoutMs);
}

bool ProtocolClient::SendPlayerState(const std::string& payloadJson) {
    return connection_.Send("player_state", payloadJson);
}

void ProtocolClient::SendDisconnect(const std::string& reason) {
    json::Object payload;
    payload["reason"] = json::Value::MakeString(reason);
    connection_.Send("disconnect", json::StringifyObject(payload));
}

void ProtocolClient::SetEventSink(std::function<void(const std::string&, const json::Object&)> sink) {
    eventSink_ = std::move(sink);
}

void ProtocolClient::SetKickHandler(std::function<void(const std::string&)> handler) {
    kickHandler_ = std::move(handler);
}

}


