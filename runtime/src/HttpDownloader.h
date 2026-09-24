#pragma once
#include <functional>
#include <string>

#include "DownloadState.h"

namespace mzzplork {

struct DownloadRequest {
    std::string host;
    unsigned short port = 80;
    std::string urlPath;      // e.g. "/resources/welcome.txt"
    std::string destPath;
    long long expectedSize = 0;
    std::string expectedHashHex;  // sha256, hex
    int timeoutMs = 15000;        // matches the bootstrapper downloader.js default
    int maxAttempts = 2;          // retries a corrupted/interrupted transfer once, like updateManager.js
};

struct DownloadResult {
    bool ok = false;
    std::string error;
    DownloadState finalState = DownloadState::Idle;
    bool resumed = false;  // true if any attempt actually resumed from a partial file
};

// HTTP GET with resume-on-interrupt via HTTP Range, sha256/size
// verification, and atomic finalize -- see docs/resources.md for the full
// behavior and docs/decisions.md for the reasoning. Downloads into
// `<destPath>.partial`; that file is only ever renamed to `destPath` once
// it has been fully verified, so a reader can never observe a partially
// downloaded file at `destPath`. Reuses the bootstrapper's downloader.js
// proven behavior (resume via Range, safe fallback when a server ignores
// Range, flush-before-reject on a dropped connection) adapted to
// WinHTTP -- see the .cpp for where the mechanism differs and why.
//
// onStateChange, if provided, is invoked (synchronously, on the caller's
// thread) on every DownloadState transition -- mainly for tests that need
// to observe that a resume/retry actually happened, not just the final
// outcome.
DownloadResult DownloadAndVerify(const DownloadRequest& request, std::function<void(DownloadState)> onStateChange = nullptr);

}
