#pragma once
#include <memory>
#include <string>
#include <vector>

#include "ErrorHandling.h"
#include "Json.h"
#include "ResourceRegistry.h"

namespace mzzplork {

// The full resource manager: manifest parsing, download, verification,
// cache placement (ApplyManifest -- entirely independent of GTA V, our
// own client talking to our own server), plus versioning, dependency
// ordering, and lifecycle (start/stop/reload/error reporting) via the
// ResourceRegistry it owns. What a resource's content actually *means*
// to gameplay is out of scope until GTA V integration exists; this only
// gets verified files onto disk and tracks their lifecycle state.
class IResourceManager {
public:
    virtual ~IResourceManager() = default;
    virtual ServiceResult Initialize(const std::string& appDataRoot) = 0;

    // Fetches/verifies/caches every changed resource in the manifest and
    // registers all of them (including ones already Cached/unchanged)
    // with the lifecycle registry as Pending -> ... -> Cached. Does not
    // load them -- call LoadAll() for that.
    virtual ServiceResult ApplyManifest(const json::Object& manifestPayload, const std::string& serverHost) = 0;

    // Dependency-ordered load of every Cached resource. Never loads a
    // resource that wasn't verified -- see ResourceRegistry.
    virtual ResourceRegistry::LoadSummary LoadAll() = 0;

    virtual bool StartResource(const std::string& id, std::string* errorOut = nullptr) = 0;
    virtual bool StopResource(const std::string& id, std::string* errorOut = nullptr) = 0;
    virtual bool ReloadResource(const std::string& id, std::string* errorOut = nullptr) = 0;

    virtual ResourceState GetResourceState(const std::string& id) const = 0;
    virtual std::vector<ResourceRecord> GetAllResources() const = 0;

    virtual bool IsInitialized() const = 0;
};

std::unique_ptr<IResourceManager> MakeFileResourceManager();

}
