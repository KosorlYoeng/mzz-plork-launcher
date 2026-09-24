#include <atomic>
#include <fstream>
#include <sstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "MinimalHttpServer.h"
#include "../src/HttpDownloader.h"
#include "../src/Sha256.h"

using namespace mzzplork;
using namespace mzzplork::test;

namespace {

std::string Payload(size_t size, char byte = 'A') {
    return std::string(size, byte);
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void WriteFile(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

DownloadRequest MakeRequest(unsigned short port, const std::string& destPath, const std::string& payload, int timeoutMs = 3000, int maxAttempts = 2) {
    DownloadRequest req;
    req.host = "127.0.0.1";
    req.port = port;
    req.urlPath = "/file";
    req.destPath = destPath;
    req.expectedSize = static_cast<long long>(payload.size());
    req.expectedHashHex = Sha256Hex(payload);
    req.timeoutMs = timeoutMs;
    req.maxAttempts = maxAttempts;
    return req;
}

std::string DestPath() {
    return mzzplork::test::MakeTempAppData() + "\\download.bin";
}

}  // namespace

// 1. Fresh download
TEST_CASE(HttpDownloader_FreshDownloadSucceeds) {
    std::string payload = Payload(5000);
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = payload;
        return resp;
    });

    std::string dest = DestPath();
    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Completed);
    ASSERT_EQ(ReadFile(dest), payload);
    ASSERT_FALSE(std::ifstream(dest + ".partial").good());
}

// 2. Resume after interruption (single call: 1st attempt drops mid-transfer, 2nd resumes and completes)
TEST_CASE(HttpDownloader_ResumesAfterInterruptionWithinOneCall) {
    std::string payload = Payload(20000, 'B');
    MinimalHttpServer server([&](const TestHttpRequest& req) {
        TestHttpResponse resp;
        if (req.rangeHeader.empty()) {
            resp.body = payload;
            resp.abortAfterBodyBytes = true;
            resp.partialBodyBytes = 8000;
            return resp;
        }
        auto dash = req.rangeHeader.find('-');
        long long start = std::stoll(req.rangeHeader.substr(6, dash - 6));
        resp.statusCode = 206;
        resp.statusText = "Partial Content";
        resp.body = payload.substr(static_cast<size_t>(start));
        return resp;
    });

    std::string dest = DestPath();
    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.resumed);
    ASSERT_EQ(ReadFile(dest), payload);
    ASSERT_EQ(server.RequestCount(), 2);
}

// 3. Resume from an existing partial file (also covers "process interruption":
// a pre-existing .partial file is exactly what's left behind if the
// process died mid-download on a previous run).
TEST_CASE(HttpDownloader_ResumesFromAPreExistingPartialFile) {
    std::string payload = Payload(9000, 'C');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", payload.substr(0, 4000));

    // Handler runs on the server's own thread -- record what it observed
    // rather than asserting there (see the "existing completed file" test
    // above for why an assertion inside the handler is unsafe).
    std::atomic<bool> sawRangeHeader{false};
    MinimalHttpServer server([&](const TestHttpRequest& req) {
        sawRangeHeader = !req.rangeHeader.empty();
        TestHttpResponse resp;
        resp.statusCode = 206;
        resp.body = payload.substr(4000);
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(sawRangeHeader);
    ASSERT_TRUE(result.resumed);
    ASSERT_EQ(ReadFile(dest), payload);
    ASSERT_EQ(server.RequestCount(), 1);  // no wasted restart-from-scratch request
}

// 4 & 7. Server supports Range, responds 206
TEST_CASE(HttpDownloader_ServerRespondingWith206IsUsedDirectly) {
    std::string payload = Payload(6000, 'D');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", payload.substr(0, 2000));

    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.statusCode = 206;
        resp.statusText = "Partial Content";
        resp.body = payload.substr(2000);
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(ReadFile(dest), payload);
}

// 5. Server does not support Range (fresh download, no pre-existing partial)
TEST_CASE(HttpDownloader_ServerIgnoringRangeStillSucceedsOnAFreshDownload) {
    std::string payload = Payload(4000, 'E');
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = payload;  // always full content, regardless of Range
        return resp;
    });

    std::string dest = DestPath();
    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(ReadFile(dest), payload);
}

// 6. Server returns 200 to a Range request -- must not append to the stale partial
TEST_CASE(HttpDownloader_200InResponseToARangeRequestDiscardsStalePartialRatherThanAppending) {
    std::string payload = Payload(5000, 'F');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", "stale-unrelated-partial-content");

    std::atomic<bool> sawRangeHeader{false};
    MinimalHttpServer server([&](const TestHttpRequest& req) {
        sawRangeHeader = !req.rangeHeader.empty();
        TestHttpResponse resp;
        resp.statusCode = 200;  // server ignores it and sends the whole file
        resp.body = payload;
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(sawRangeHeader);  // client did ask for a range
    ASSERT_TRUE(result.ok);
    ASSERT_FALSE(result.resumed);
    ASSERT_EQ(ReadFile(dest), payload);  // not "stale-unrelated-partial-content" + payload
}

// 8. HTTP 416 -- invalid range, must discard and restart from zero
TEST_CASE(HttpDownloader_416CausesARestartFromZero) {
    std::string payload = Payload(3000, 'G');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", Payload(500, 'X'));  // now-invalid partial, e.g. the resource changed server-side

    MinimalHttpServer server([&](const TestHttpRequest& req) {
        TestHttpResponse resp;
        if (!req.rangeHeader.empty()) {
            resp.statusCode = 416;
            resp.statusText = "Range Not Satisfiable";
            return resp;
        }
        resp.body = payload;
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(ReadFile(dest), payload);
    ASSERT_EQ(server.RequestCount(), 2);
}

// HTTP 404 -- permanent failure, partial state cleaned up, no retry
TEST_CASE(HttpDownloader_404IsAPermanentFailureThatCleansUpThePartialFile) {
    std::string payload = Payload(1000, 'H');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", payload.substr(0, 200));

    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.statusCode = 404;
        resp.statusText = "Not Found";
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Failed);
    ASSERT_EQ(server.RequestCount(), 1);  // not retried -- permanent
    ASSERT_FALSE(std::ifstream(dest + ".partial").good());
}

// HTTP 5xx -- treated as transient/retryable
TEST_CASE(HttpDownloader_5xxIsTreatedAsTransientAndRetried) {
    std::string payload = Payload(1500, 'I');
    int callCount = 0;
    MinimalHttpServer server([&](const TestHttpRequest&) {
        callCount++;
        TestHttpResponse resp;
        if (callCount == 1) {
            resp.statusCode = 503;
            resp.statusText = "Service Unavailable";
            return resp;
        }
        resp.body = payload;
        return resp;
    });

    std::string dest = DestPath();
    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(server.RequestCount(), 2);
}

// 9. Connection interruption -- partial preserved, reported as a failure, not silently "succeeded"
TEST_CASE(HttpDownloader_ConnectionInterruptionIsNeverReportedAsSuccess) {
    std::string payload = Payload(20000, 'J');
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = payload;
        resp.abortAfterBodyBytes = true;
        resp.partialBodyBytes = 5000;
        return resp;
    });

    std::string dest = DestPath();
    auto req = MakeRequest(server.Port(), dest, payload, 3000, /*maxAttempts=*/1);
    auto result = DownloadAndVerify(req);

    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Failed);
    ASSERT_FALSE(std::ifstream(dest).good());  // never exposed as a completed file
    // Transient interruption -- partial preserved for a future resume, not deleted.
    ASSERT_TRUE(std::ifstream(dest + ".partial").good());
}

// 10. Timeout
TEST_CASE(HttpDownloader_TimeoutIsReportedAsAFailureNotAHang) {
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.hangForever = true;
        return resp;
    });

    std::string dest = DestPath();
    auto req = MakeRequest(server.Port(), dest, Payload(100), /*timeoutMs=*/200, /*maxAttempts=*/1);
    auto result = DownloadAndVerify(req);

    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Failed);
}

// 11. Hash verification success (implicit in most tests above, explicit here)
TEST_CASE(HttpDownloader_HashVerificationSucceedsForCorrectContent) {
    std::string payload = Payload(2222, 'K');
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = payload;
        return resp;
    });

    std::string dest = DestPath();
    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
}

// 12. Hash verification failure
TEST_CASE(HttpDownloader_HashVerificationFailureInvalidatesTheFile) {
    std::string payload = Payload(2222, 'L');
    std::string wrongPayload = Payload(2222, 'M');
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = wrongPayload;  // server sends different bytes than the manifest promised
        return resp;
    });

    std::string dest = DestPath();
    auto req = MakeRequest(server.Port(), dest, payload, 3000, /*maxAttempts=*/1);
    auto result = DownloadAndVerify(req);

    ASSERT_FALSE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Failed);
    ASSERT_FALSE(std::ifstream(dest).good());
    ASSERT_FALSE(std::ifstream(dest + ".partial").good());  // permanently invalid -- cleaned up
}

// 13. Zero-byte partial file
TEST_CASE(HttpDownloader_ZeroByteExistingPartialIsTreatedAsNoPartialAtAll) {
    std::string payload = Payload(1000, 'N');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", "");

    std::atomic<bool> sawRangeHeader{false};
    MinimalHttpServer server([&](const TestHttpRequest& req) {
        sawRangeHeader = !req.rangeHeader.empty();
        TestHttpResponse resp;
        resp.body = payload;
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_FALSE(sawRangeHeader);  // offset 0 -- no Range header sent at all
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(ReadFile(dest), payload);
}

// 14. Partial file larger than expected
TEST_CASE(HttpDownloader_OversizedPartialFileIsDiscardedAndRestarted) {
    std::string payload = Payload(1000, 'O');
    std::string dest = DestPath();
    WriteFile(dest + ".partial", Payload(5000, 'Z'));  // larger than expectedSize

    std::atomic<bool> sawRangeHeader{false};
    MinimalHttpServer server([&](const TestHttpRequest& req) {
        sawRangeHeader = !req.rangeHeader.empty();
        TestHttpResponse resp;
        resp.body = payload;
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_FALSE(sawRangeHeader);  // discarded -- fresh request from zero
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(ReadFile(dest), payload);
}

// 15. Existing completed file
TEST_CASE(HttpDownloader_ExistingCorrectFileIsRecognizedWithoutAnyRequest) {
    std::string payload = Payload(1000, 'P');
    std::string dest = DestPath();
    WriteFile(dest, payload);

    // The handler runs on the server's own accept thread, so it must not
    // throw (an AssertionFailure escaping there would call std::terminate()
    // and take down the whole test binary, not just this test) -- instead
    // it returns an obviously-wrong response, and the real assertion below
    // (RequestCount() == 0, checked from the main thread) is what actually
    // proves this handler was never invoked.
    MinimalHttpServer server([&](const TestHttpRequest&) -> TestHttpResponse {
        TestHttpResponse resp;
        resp.statusCode = 500;
        return resp;
    });

    auto result = DownloadAndVerify(MakeRequest(server.Port(), dest, payload));

    ASSERT_TRUE(result.ok);
    ASSERT_TRUE(result.finalState == DownloadState::Completed);
    ASSERT_EQ(server.RequestCount(), 0);
}

// State transition observability
TEST_CASE(HttpDownloader_ReportsStateTransitionsThroughACallback) {
    std::string payload = Payload(3000, 'Q');
    MinimalHttpServer server([&](const TestHttpRequest&) {
        TestHttpResponse resp;
        resp.body = payload;
        return resp;
    });

    std::vector<DownloadState> states;
    std::string dest = DestPath();
    DownloadAndVerify(MakeRequest(server.Port(), dest, payload), [&](DownloadState s) { states.push_back(s); });

    ASSERT_TRUE(states.size() >= 3);
    ASSERT_TRUE(states.front() == DownloadState::Downloading);
    ASSERT_TRUE(states.back() == DownloadState::Completed);
    bool sawVerifying = false;
    for (auto s : states) if (s == DownloadState::Verifying) sawVerifying = true;
    ASSERT_TRUE(sawVerifying);
}
