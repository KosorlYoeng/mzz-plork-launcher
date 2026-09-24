#pragma once
#include <string>

namespace mzzplork {

// Deterministic per-resource lifecycle state, following the same
// enum-class + ToString() convention as RuntimeState/ClientState/
// DownloadState -- the project's existing pattern, not a new one.
//
//   Pending -> Downloading -> Verifying -> Cached -> Loading -> Loaded -> Started
//                  |              |                     |
//                  v              v                     v
//                Failed         Failed                Failed
//   Started <-> Stopped (Start()/Stop())
//   Started|Stopped -> Loading (Reload())
//
// "Cached" means verified bytes are present on disk -- Loading can only
// ever be reached from Cached, never from Pending/Downloading/Verifying
// directly, so an unverified file can never load. See IsValidTransition.
enum class ResourceState {
    Pending,
    Downloading,
    Verifying,
    Cached,
    Loading,
    Loaded,
    Started,
    Stopped,
    Failed
};

const char* ToString(ResourceState state);

// Guards every state change against the table above. Not a formality --
// this is what makes "do not allow unverified files to load" an
// enforced invariant rather than a convention callers have to remember.
bool IsValidTransition(ResourceState from, ResourceState to);

}
