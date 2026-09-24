#include "HttpDownloader.h"
#include "Sha256.h"

#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace mzzplork {

namespace fs = std::filesystem;

namespace {

std::wstring ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), result.data(), len);
    return result;
}

// RAII so every early-return path in PerformSingleAttempt still closes
// whatever WinHTTP handles it opened.
struct WinHttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    ~WinHttpHandles() {
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
    }
};

enum class AttemptOutcome {
    Completed,        // the full response body was received and written
    Interrupted,      // connection/timeout error mid-transfer -- retryable, partial data preserved
    HardFailure,      // permanent HTTP error (404, unexpected status) -- not retryable
    RestartFromZero   // 416 Range Not Satisfiable -- the requested offset is invalid; caller should discard and retry from 0
};

struct AttemptResult {
    AttemptOutcome outcome;
    std::string error;
    bool usedRange = false;  // true only if the server actually honored our Range request (206)
};

bool FileMatchesExpected(const std::string& path, long long expectedSize, const std::string& expectedHashHex) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;
    auto size = static_cast<long long>(fs::file_size(path, ec));
    if (ec) return false;
    if (expectedSize > 0 && size != expectedSize) return false;
    if (!expectedHashHex.empty()) {
        try {
            if (Sha256HexOfFile(path) != expectedHashHex) return false;
        } catch (...) {
            return false;
        }
    }
    return true;
}

// Performs one GET (with a Range header if offset > 0) and writes the
// response body to partialPath. Reuses downloader.js's proven behavior
// -- resume via Range; if the server ignores Range and returns 200
// instead of 206, never append that response to the existing partial
// data -- but adapts the mechanism: rather than aborting and issuing a
// second request (as downloader.js does), a 200 response is simply
// written in truncate mode as the complete file, since the body it just
// sent already *is* the whole resource from byte 0. Same safety
// guarantee (a range-not-supported server never corrupts the partial
// file by appending), one fewer round trip.
AttemptResult PerformSingleAttempt(const DownloadRequest& request, const std::string& partialPath, long long offset) {
    WinHttpHandles h;
    h.session = WinHttpOpen(L"MzzPlorkClient/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) return {AttemptOutcome::Interrupted, "WinHttpOpen failed"};

    WinHttpSetTimeouts(h.session, request.timeoutMs, request.timeoutMs, request.timeoutMs, request.timeoutMs);

    h.connection = WinHttpConnect(h.session, ToWide(request.host).c_str(), request.port, 0);
    if (!h.connection) return {AttemptOutcome::Interrupted, "WinHttpConnect failed"};

    h.request = WinHttpOpenRequest(
        h.connection, L"GET", ToWide(request.urlPath).c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!h.request) return {AttemptOutcome::Interrupted, "WinHttpOpenRequest failed"};

    std::wstring rangeHeader;
    if (offset > 0) {
        rangeHeader = L"Range: bytes=" + std::to_wstring(offset) + L"-";
    }

    bool sent = WinHttpSendRequest(
                    h.request,
                    rangeHeader.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : rangeHeader.c_str(),
                    rangeHeader.empty() ? 0 : static_cast<DWORD>(rangeHeader.size()),
                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(h.request, nullptr);

    if (!sent) {
        DWORD err = GetLastError();
        if (err == ERROR_WINHTTP_TIMEOUT) {
            return {AttemptOutcome::Interrupted, "request timed out"};
        }
        return {AttemptOutcome::Interrupted, "HTTP request failed (error " + std::to_string(err) + ")"};
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(h.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode == 416) {
        return {AttemptOutcome::RestartFromZero, "HTTP 416 Range Not Satisfiable"};
    }
    if (statusCode == 404) {
        return {AttemptOutcome::HardFailure, "HTTP 404"};
    }
    if (statusCode >= 500) {
        return {AttemptOutcome::Interrupted, "HTTP " + std::to_string(statusCode)};
    }
    if (statusCode != 200 && statusCode != 206) {
        return {AttemptOutcome::HardFailure, "unexpected HTTP status " + std::to_string(statusCode)};
    }

    bool serverHonoredRange = (statusCode == 206);
    // A 200 means "here is the whole resource from byte 0" regardless of
    // whether we asked for a range -- write it as a fresh file, never
    // appended to whatever partial bytes we already had.
    bool appendMode = serverHonoredRange;

    // Content-Length is the authoritative signal for "did we actually get
    // everything." WinHTTP's read loop below can end "normally" (available
    // hits 0, no WinHttpReadData error) even when the underlying connection
    // was reset mid-stream -- a plain TCP close without an explicit error
    // looks the same as a clean end-of-body to that loop. Relying only on
    // "the loop ended without erroring" would misclassify a truncated
    // transfer as Completed; comparing bytes actually received against
    // Content-Length catches it here instead of only much later, as a
    // confusing size-mismatch verification failure that would (wrongly)
    // discard an otherwise-resumable partial file.
    DWORD contentLength = 0;
    DWORD contentLengthSize = sizeof(contentLength);
    bool hasContentLength = WinHttpQueryHeaders(
        h.request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &contentLength, &contentLengthSize, WINHTTP_NO_HEADER_INDEX);

    DWORD totalBytesThisResponse = 0;
    {
        std::ofstream out(partialPath, std::ios::binary | (appendMode ? std::ios::app : std::ios::trunc));
        if (!out) {
            return {AttemptOutcome::Interrupted, "could not open partial file for writing: " + partialPath};
        }

        std::vector<char> buffer(65536);
        DWORD available = 0;
        while (WinHttpQueryDataAvailable(h.request, &available) && available > 0) {
            DWORD toRead = static_cast<DWORD>(std::min<size_t>(available, buffer.size()));
            DWORD bytesRead = 0;
            if (!WinHttpReadData(h.request, buffer.data(), toRead, &bytesRead)) {
                DWORD err = GetLastError();
                out.close();  // flush whatever was written before reporting the interruption
                if (err == ERROR_WINHTTP_TIMEOUT) {
                    return {AttemptOutcome::Interrupted, "connection timed out mid-transfer", serverHonoredRange};
                }
                return {AttemptOutcome::Interrupted, "connection dropped mid-transfer (error " + std::to_string(err) + ")", serverHonoredRange};
            }
            out.write(buffer.data(), bytesRead);
            totalBytesThisResponse += bytesRead;
        }
    }

    if (hasContentLength && totalBytesThisResponse != contentLength) {
        return {
            AttemptOutcome::Interrupted,
            "connection closed before the full response body arrived (" + std::to_string(totalBytesThisResponse)
                + " of " + std::to_string(contentLength) + " bytes)",
            serverHonoredRange};
    }

    return {AttemptOutcome::Completed, "", serverHonoredRange};
}

}  // namespace

DownloadResult DownloadAndVerify(const DownloadRequest& request, std::function<void(DownloadState)> onStateChange) {
    auto setState = [&](DownloadState s) {
        if (onStateChange) onStateChange(s);
    };

    DownloadResult result;
    result.finalState = DownloadState::Idle;

    if (FileMatchesExpected(request.destPath, request.expectedSize, request.expectedHashHex)) {
        setState(DownloadState::Completed);
        result.ok = true;
        result.finalState = DownloadState::Completed;
        return result;
    }

    std::error_code dirEc;
    fs::create_directories(fs::path(request.destPath).parent_path(), dirEc);

    std::string partialPath = request.destPath + ".partial";

    long long offset = 0;
    {
        std::error_code ec;
        if (fs::exists(partialPath, ec)) {
            auto size = static_cast<long long>(fs::file_size(partialPath, ec));
            if (!ec) {
                if (request.expectedSize > 0 && size >= request.expectedSize) {
                    fs::remove(partialPath, ec);
                    offset = 0;
                } else {
                    offset = size;
                }
            }
        }
    }

    bool everResumed = false;
    bool completed = false;
    std::string lastError;

    int maxAttempts = request.maxAttempts > 0 ? request.maxAttempts : 1;
    for (int attempt = 1; attempt <= maxAttempts; attempt++) {
        setState(offset > 0 ? DownloadState::Resuming : DownloadState::Downloading);

        AttemptResult attemptResult = PerformSingleAttempt(request, partialPath, offset);

        if (attemptResult.outcome == AttemptOutcome::Completed) {
            everResumed = everResumed || attemptResult.usedRange;
            completed = true;
            break;
        }

        if (attemptResult.outcome == AttemptOutcome::HardFailure) {
            std::error_code ec;
            fs::remove(partialPath, ec);
            setState(DownloadState::Failed);
            result.ok = false;
            result.error = attemptResult.error;
            result.finalState = DownloadState::Failed;
            return result;
        }

        if (attemptResult.outcome == AttemptOutcome::RestartFromZero) {
            std::error_code ec;
            fs::remove(partialPath, ec);
            offset = 0;
            lastError = attemptResult.error;
            continue;
        }

        // Interrupted: preserve whatever was written and retry, resuming
        // from wherever the partial file actually ended up.
        setState(DownloadState::Interrupted);
        lastError = attemptResult.error;
        std::error_code ec;
        offset = fs::exists(partialPath, ec) ? static_cast<long long>(fs::file_size(partialPath, ec)) : 0;
        if (ec) offset = 0;
    }

    if (!completed) {
        // Exhausting attempts here only ever happens via repeated
        // Interrupted outcomes -- HardFailure and RestartFromZero-exhaustion
        // both already returned (and cleaned up) earlier. An interruption is
        // transient, not "permanently invalid": preserve the partial file so
        // a future call (a later run of the process, say) can resume from it
        // instead of restarting the whole transfer, per "do not restart the
        // entire download unnecessarily."
        setState(DownloadState::Failed);
        result.ok = false;
        result.error = "download did not complete after " + std::to_string(maxAttempts) + " attempt(s): " + lastError;
        result.finalState = DownloadState::Failed;
        return result;
    }

    setState(DownloadState::Verifying);

    std::error_code sizeEc;
    auto actualSize = static_cast<long long>(fs::file_size(partialPath, sizeEc));
    if (sizeEc) {
        setState(DownloadState::Failed);
        result.ok = false;
        result.error = "could not stat partial file after download: " + sizeEc.message();
        result.finalState = DownloadState::Failed;
        return result;
    }
    if (request.expectedSize > 0 && actualSize != request.expectedSize) {
        std::error_code ec;
        fs::remove(partialPath, ec);
        setState(DownloadState::Failed);
        result.ok = false;
        result.error = "size mismatch: expected " + std::to_string(request.expectedSize) + ", got " + std::to_string(actualSize);
        result.finalState = DownloadState::Failed;
        return result;
    }

    if (!request.expectedHashHex.empty()) {
        std::string actualHash = Sha256HexOfFile(partialPath);
        if (actualHash != request.expectedHashHex) {
            std::error_code ec;
            fs::remove(partialPath, ec);
            setState(DownloadState::Failed);
            result.ok = false;
            result.error = "hash mismatch: expected " + request.expectedHashHex + ", got " + actualHash;
            result.finalState = DownloadState::Failed;
            return result;
        }
    }

    // Atomic finalize: only a fully-verified file ever becomes destPath.
    std::error_code renameEc;
    fs::rename(partialPath, request.destPath, renameEc);
    if (renameEc) {
        setState(DownloadState::Failed);
        result.ok = false;
        result.error = "could not finalize downloaded file: " + renameEc.message();
        result.finalState = DownloadState::Failed;
        return result;
    }

    setState(DownloadState::Completed);
    result.ok = true;
    result.finalState = DownloadState::Completed;
    result.resumed = everResumed;
    return result;
}

}


