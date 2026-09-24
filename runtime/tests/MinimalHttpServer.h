#pragma once
// A minimal, test-only HTTP/1.1 server over raw Winsock sockets. There is
// no HTTP server building block anywhere in this codebase or the C++
// standard library (WinHTTP is a client library only), so this exists
// purely to give HttpDownloaderTests.cpp the same kind of fine-grained
// control over server behavior that the bootstrapper's JS tests get for
// free from Node's http module (partial responses, dropped connections,
// Range handling, deliberate delays).
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace mzzplork::test {

struct TestHttpRequest {
    std::string method;
    std::string path;
    std::string rangeHeader;  // value after "Range: ", empty if the header was absent
};

struct TestHttpResponse {
    int statusCode = 200;
    std::string statusText = "OK";
    std::vector<std::pair<std::string, std::string>> headers;  // Content-Length is added automatically from body.size() unless already present
    std::string body;

    // Simulates a connection reset: sends only the first partialBodyBytes
    // of `body` (headers are sent in full) then aborts the socket rather
    // than closing it cleanly.
    bool abortAfterBodyBytes = false;
    size_t partialBodyBytes = 0;

    int delayBeforeRespondingMs = 0;  // simulates a slow server
    bool hangForever = false;         // accepts the connection, reads the request, then never responds -- a pure timeout scenario
};

using Handler = std::function<TestHttpResponse(const TestHttpRequest&)>;

class MinimalHttpServer {
public:
    explicit MinimalHttpServer(Handler handler);
    ~MinimalHttpServer();

    MinimalHttpServer(const MinimalHttpServer&) = delete;
    MinimalHttpServer& operator=(const MinimalHttpServer&) = delete;

    unsigned short Port() const { return port_; }
    int RequestCount() const { return requestCount_; }

private:
    void AcceptLoop();
    void HandleConnection(SOCKET client);

    Handler handler_;
    SOCKET listenSocket_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::atomic<bool> shouldStop_{false};
    std::atomic<int> requestCount_{0};
    std::thread acceptThread_;
    bool wsaStarted_ = false;
};

}
