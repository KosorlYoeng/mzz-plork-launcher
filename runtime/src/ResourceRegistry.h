#pragma once
#include <map>
#include <string>
#include <vector>

#include "ResourceState.h"

namespace mzzplork {

struct ResourceEntry {
    std::string id;
    std::string path;
    long long size = 0;
    std::string hash;
    std::string version;
    std::vector<std::string> dependencies;  // ids of other resources in the same manifest
};

struct ResourceRecord {
    ResourceEntry entry;
    ResourceState state = ResourceState::Pending;
    std::string error;
};

// Owns the per-resource state machine and dependency graph -- the
// "versioning, dependency ordering, resource lifecycle (start/stop/
// reload), error reporting" responsibilities. ResourceManager (which
// owns download/verify/cache) drives this as each file is acquired;
// nothing here touches the network.
//
// Central invariant: Loading is only ever reachable from Cached
// (enforced by ResourceState::IsValidTransition), so a resource whose
// bytes were never downloaded-and-verified can never load, regardless of
// dependency ordering.
class ResourceRegistry {
public:
    // resourcesDir: the directory entry.path is resolved against for the
    // freshness check LoadOne() performs (e.g. "<appDataRoot>\resources").
    explicit ResourceRegistry(std::string resourcesDir);

    // Resets the registry for a freshly-fetched manifest. Every entry
    // starts Pending. Call once per ApplyManifest().
    void LoadManifest(const std::vector<ResourceEntry>& entries);

    // Records a state transition for one resource (e.g. as
    // ResourceManager downloads/verifies it). Returns false (and leaves
    // the state unchanged) if the transition isn't valid per
    // ResourceState::IsValidTransition -- callers should treat that as a
    // logic error, not something to silently ignore.
    bool TransitionTo(const std::string& id, ResourceState newState, const std::string& error = "");

    struct LoadSummary {
        int loaded = 0;
        int failed = 0;
        std::vector<std::pair<std::string, std::string>> errors;  // id -> message
    };

    // Dependency-ordered Cached -> Loading -> Loaded for every resource
    // that reached Cached (i.e. was actually verified). A resource whose
    // dependency is missing from the manifest, failed, or is part of a
    // dependency cycle is marked Failed with a specific reason instead of
    // silently skipped -- the failure cascades to anything that
    // (transitively) depends on it, but resources outside the affected
    // subgraph still load normally.
    LoadSummary LoadAll();

    // Started -> Stopped.
    bool Stop(const std::string& id, std::string* errorOut = nullptr);
    // Loaded -> Started, or Stopped -> Started.
    bool Start(const std::string& id, std::string* errorOut = nullptr);
    // Re-validates a resource's on-disk content without re-downloading
    // (Started|Stopped -> Loading -> Loaded, restarted afterward if it
    // was Started beforehand). Re-fetching a changed file is
    // ResourceManager's job (a fresh ApplyManifest call), not this.
    bool Reload(const std::string& id, std::string* errorOut = nullptr);

    ResourceState GetState(const std::string& id) const;
    std::string GetError(const std::string& id) const;
    std::vector<ResourceRecord> GetAll() const;
    bool Contains(const std::string& id) const;

private:
    bool LoadOne(const std::string& id);  // Cached -> Loading -> Loaded|Failed; validates the file is still present/correctly sized

    std::string resourcesDir_;
    std::map<std::string, ResourceRecord> records_;
};

}
