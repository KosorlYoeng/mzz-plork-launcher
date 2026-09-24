#include "MinimalHttpServer.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace mzzplork::test {

namespace {

std::string ReadRequestHeaders(SOCKET client) {
    std::string buffer;
    char chunk[4096];
    while (buffer.find("\r\n\r\n") == std::string::npos && buffer.size() < 65536) {
        int received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) break;
        buffer.append(chunk, received);
    }
    return buffer;
}

TestHttpRequest ParseRequest(const std::string& raw) {
    TestHttpRequest req;
    std::istringstream stream(raw);
    std::string requestLine;
    std::getline(stream, requestLine);
    std::istringstream lineStream(requestLine);
    lineStream >> req.method >> req.path;

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (_stricmp(name.c_str(), "Range") == 0) {
            req.rangeHeader = value;
        }
    }
    return req;
}

std::string BuildResponseHeader(const TestHttpResponse& resp) {
    std::ostringstream out;
    out << "HTTP/1.1 " << resp.statusCode << " " << resp.statusText << "\r\n";
    bool hasContentLength = false;
    for (auto& [name, value] : resp.headers) {
        out << name << ": " << value << "\r\n";
        if (_stricmp(name.c_str(), "Content-Length") == 0) hasContentLength = true;
    }
    if (!hasContentLength) {
        out << "Content-Length: " << resp.body.size() << "\r\n";
    }
    out << "Connection: close\r\n\r\n";
    return out.str();
}

}  // namespace

MinimalHttpServer::MinimalHttpServer(Handler handler) : handler_(std::move(handler)) {
    WSADATA wsaData;
    wsaStarted_ = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;

    listenSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;  // ephemeral
    bind(listenSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    int addrLen = sizeof(addr);
    getsockname(listenSocket_, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    port_ = ntohs(addr.sin_port);

    listen(listenSocket_, 8);

    acceptThread_ = std::thread([this] { AcceptLoop(); });
}

MinimalHttpServer::~MinimalHttpServer() {
    shouldStop_ = true;
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
    if (wsaStarted_) WSACleanup();
}

void MinimalHttpServer::AcceptLoop() {
    while (!shouldStop_) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket_, &readSet);
        timeval timeout{0, 200000};  // 200ms, so shouldStop_ is checked regularly
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready <= 0) continue;

        SOCKET client = accept(listenSocket_, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        HandleConnection(client);
    }
}

void MinimalHttpServer::HandleConnection(SOCKET client) {
    std::string raw = ReadRequestHeaders(client);
    TestHttpRequest req = ParseRequest(raw);
    requestCount_++;

    TestHttpResponse resp = handler_(req);

    if (resp.delayBeforeRespondingMs > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(resp.delayBeforeRespondingMs));
    }

    if (resp.hangForever) {
        // Never respond. The client's own configured timeout is what ends
        // this. Give the client generous room to time out, then clean up
        // our side so the test doesn't leak a thread past its scope.
        for (int waited = 0; waited < 20000 && !shouldStop_; waited += 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        closesocket(client);
        return;
    }

    std::string header = BuildResponseHeader(resp);
    send(client, header.data(), static_cast<int>(header.size()), 0);

    if (resp.abortAfterBodyBytes) {
        size_t n = (std::min)(resp.partialBodyBytes, resp.body.size());
        if (n > 0) send(client, resp.body.data(), static_cast<int>(n), 0);
        // Abrupt close (no shutdown()) simulates a reset rather than a clean FIN.
        closesocket(client);
        return;
    }

    if (!resp.body.empty()) {
        send(client, resp.body.data(), static_cast<int>(resp.body.size()), 0);
    }
    shutdown(client, SD_SEND);
    closesocket(client);
}

}
