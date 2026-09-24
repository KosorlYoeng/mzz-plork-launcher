#pragma once
#include <string>

namespace mzzplork {

// Explicit download state, following the same enum-class + ToString()
// pattern already used by RuntimeState (Lifecycle.h) and ClientState --
// not a new abstraction, the existing project convention applied here.
enum class DownloadState {
    Idle,
    Downloading,
    Interrupted,
    Resuming,
    Verifying,
    Completed,
    Failed
};

const char* ToString(DownloadState state);

}
