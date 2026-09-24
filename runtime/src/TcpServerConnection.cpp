#include "TcpServerConnection.h"
#include "Json.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace mzzplork {

namespace {

class TcpServerConnection : public IServerConnection {
public:
    ~TcpServerConnection() override { Disconnect(); }

    bool Connect(const std::string& host, unsigned short port) override {
        if (connected_) return true;

        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
        wsaStarted_ = true;

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        std::string portStr = std::to_string(port);
        if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) {
            CleanupWinsock();
            return false;
        }

        SOCKET sock = INVALID_SOCKET;
        addrinfo* attempt = result;
        for (; attempt != nullptr; attempt = attempt->ai_next) {
            sock = socket(attempt->ai_family, attempt->ai_socktype, attempt->ai_protocol);
            if (sock == INVALID_SOCKET) continue;
            if (connect(sock, attempt->ai_addr, static_cast<int>(attempt->ai_addrlen)) == 0) break;
            closesocket(sock);
            sock = INVALID_SOCKET;
        }
        freeaddrinfo(result);

        if (sock == INVALID_SOCKET) {
            CleanupWinsock();
            return false;
        }

        // Bounded receive timeout so the reader thread can periodically
        // check shouldStop_ and exit cleanly on Disconnect(), rather than
        // blocking forever inside recv().
        DWORD timeoutMs = 500;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        socket_ = sock;
        connected_ = true;
        shouldStop_ = false;
        readerThread_ = std::thread([this] { ReaderLoop(); });
        return true;
    }

    void Disconnect() override {
        if (!connected_) return;
        shouldStop_ = true;
        connected_ = false;
        // Half-close the send side first (clean FIN, lets the peer see an
        // orderly disconnect instead of a reset) and let the reader thread
        // notice shouldStop_ and exit on its own -- only close the handle
        // once nothing else is touching it. Closing the socket while the
        // reader thread might still be inside recv() is exactly what
        // produces a spurious ECONNRESET on the peer's side instead of a
        // graceful shutdown.
        if (socket_ != INVALID_SOCKET) {
            shutdown(socket_, SD_SEND);
        }
        if (readerThread_.joinable()) readerThread_.join();
        if (socket_ != INVALID_SOCKET) {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }
        CleanupWinsock();
    }

    bool IsConnected() const override { return connected_; }

    bool SendHandshake() override {
        return Send("hello", "{}");
    }

    bool Send(const std::string& type, const std::string& payloadJson) override {
        if (!connected_ || socket_ == INVALID_SOCKET) return false;
        std::string line = "{\"type\":\"" + type + "\",\"payload\":" + (payloadJson.empty() ? "{}" : payloadJson) + "}\n";
        std::lock_guard<std::mutex> lock(sendMutex_);
        int total = 0;
        int len = static_cast<int>(line.size());
        while (total < len) {
            int sent = send(socket_, line.data() + total, len - total, 0);
            if (sent == SOCKET_ERROR) return false;
            total += sent;
        }
        return true;
    }

    void SetMessageHandler(std::function<void(const ServerMessage&)> handler) override {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handler_ = std::move(handler);
    }

private:
    void CleanupWinsock() {
        if (wsaStarted_) {
            WSACleanup();
            wsaStarted_ = false;
        }
    }

    void ReaderLoop() {
        char buf[4096];
        while (!shouldStop_) {
            int received = recv(socket_, buf, sizeof(buf), 0);
            if (received > 0) {
                readBuffer_.append(buf, received);
                DispatchCompleteLines();
            } else if (received == 0) {
                break;  // peer closed the connection
            } else {
                int err = WSAGetLastError();
                if (err == WSAETIMEDOUT) continue;  // expected, lets us re-check shouldStop_
                break;  // real socket error
            }
        }
        connected_ = false;
    }

    void DispatchCompleteLines() {
        size_t newlinePos;
        while ((newlinePos = readBuffer_.find('\n')) != std::string::npos) {
            std::string line = readBuffer_.substr(0, newlinePos);
            readBuffer_.erase(0, newlinePos + 1);
            if (line.empty()) continue;

            auto parsed = json::Parse(line);
            if (!parsed.ok || parsed.value.type != json::ValueType::Object) continue;
            auto typeIt = parsed.value.objectValue.find("type");
            if (typeIt == parsed.value.objectValue.end() || typeIt->second.type != json::ValueType::String) continue;
            std::string type = typeIt->second.stringValue;

            auto payloadIt = parsed.value.objectValue.find("payload");
            std::string payload = payloadIt != parsed.value.objectValue.end() ? json::Stringify(payloadIt->second) : "{}";

            std::function<void(const ServerMessage&)> handlerCopy;
            {
                std::lock_guard<std::mutex> lock(handlerMutex_);
                handlerCopy = handler_;
            }
            if (handlerCopy) handlerCopy(ServerMessage{type, payload});
        }
    }

    SOCKET socket_ = INVALID_SOCKET;
    std::atomic<bool> connected_{false};
    std::atomic<bool> shouldStop_{false};
    bool wsaStarted_ = false;
    std::thread readerThread_;
    std::string readBuffer_;
    std::mutex sendMutex_;
    std::mutex handlerMutex_;
    std::function<void(const ServerMessage&)> handler_;
};

}  // namespace

std::unique_ptr<IServerConnection> MakeTcpServerConnection() {
    return std::make_unique<TcpServerConnection>();
}

}


