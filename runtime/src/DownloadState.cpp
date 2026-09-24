#include "DownloadState.h"

namespace mzzplork {

const char* ToString(DownloadState state) {
    switch (state) {
        case DownloadState::Idle: return "idle";
        case DownloadState::Downloading: return "downloading";
        case DownloadState::Interrupted: return "interrupted";
        case DownloadState::Resuming: return "resuming";
        case DownloadState::Verifying: return "verifying";
        case DownloadState::Completed: return "completed";
        case DownloadState::Failed: return "failed";
    }
    return "unknown";
}

}
