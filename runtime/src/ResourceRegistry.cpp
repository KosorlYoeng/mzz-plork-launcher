#include "ResourceRegistry.h"

#include <deque>
#include <filesystem>
#include <set>

namespace mzzplork {

namespace fs = std::filesystem;

ResourceRegistry::ResourceRegistry(std::string resourcesDir) : resourcesDir_(std::move(resourcesDir)) {}

void ResourceRegistry::LoadManifest(const std::vector<ResourceEntry>& entries) {
    records_.clear();
    for (const auto& entry : entries) {
        ResourceRecord record;
        record.entry = entry;
        record.state = ResourceState::Pending;
        records_[entry.id] = record;
    }
}

bool ResourceRegistry::TransitionTo(const std::string& id, ResourceState newState, const std::string& error) {
    auto it = records_.find(id);
    if (it == records_.end()) return false;
    if (!IsValidTransition(it->second.state, newState)) return false;
    it->second.state = newState;
    it->second.error = error;
    return true;
}

namespace {
// Shared by LoadOne() and Reload(): from Loading, checks the file is
// still present and correctly sized, then transitions to Loaded or
// Failed. Assumes the caller already made the Cached->Loading or
// Started/Stopped->Loading transition.
bool ValidateAndMarkLoaded(ResourceRegistry& registry, const std::string& resourcesDir, const std::string& id, const ResourceEntry& entry) {
    std::string fullPath = resourcesDir + "\\" + entry.path;
    std::error_code ec;
    if (!fs::exists(fullPath, ec) || ec) {
        registry.TransitionTo(id, ResourceState::Failed, "file missing during load: " + fullPath);
        return false;
    }
    auto size = static_cast<long long>(fs::file_size(fullPath, ec));
    if (ec || (entry.size > 0 && size != entry.size)) {
        registry.TransitionTo(id, ResourceState::Failed, "file changed on disk since verification: " + fullPath);
        return false;
    }
    registry.TransitionTo(id, ResourceState::Loaded);
    return true;
}
}  // namespace

bool ResourceRegistry::LoadOne(const std::string& id) {
    auto it = records_.find(id);
    if (it == records_.end()) return false;
    if (!TransitionTo(id, ResourceState::Loading)) return false;
    return ValidateAndMarkLoaded(*this, resourcesDir_, id, it->second.entry);
}

ResourceRegistry::LoadSummary ResourceRegistry::LoadAll() {
    LoadSummary summary;

    // Only Cached (verified) resources are eligible -- anything else
    // (still Pending, or Failed from a download error) is left alone;
    // "do not allow unverified files to load" applies here structurally,
    // not just by convention.
    std::vector<std::string> eligible;
    for (const auto& [id, record] : records_) {
        if (record.state == ResourceState::Cached) eligible.push_back(id);
    }

    // Fail anything whose dependency is missing from the manifest or
    // already failed -- before attempting a topological sort, so these
    // don't participate in it at all.
    std::set<std::string> eligibleSet(eligible.begin(), eligible.end());
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& id : eligible) {
            auto& record = records_[id];
            if (record.state != ResourceState::Cached) continue;  // already resolved (failed) this pass
            for (const auto& dep : record.entry.dependencies) {
                auto depIt = records_.find(dep);
                if (depIt == records_.end()) {
                    TransitionTo(id, ResourceState::Failed, "missing dependency: " + dep);
                    summary.failed++;
                    summary.errors.emplace_back(id, records_[id].error);
                    changed = true;
                    break;
                }
                if (depIt->second.state == ResourceState::Failed) {
                    TransitionTo(id, ResourceState::Failed, "dependency failed: " + dep);
                    summary.failed++;
                    summary.errors.emplace_back(id, records_[id].error);
                    changed = true;
                    break;
                }
            }
        }
    }

    // Kahn's algorithm over the remaining still-Cached resources.
    // in-degree = number of not-yet-satisfied dependencies within this
    // eligible set (a dependency outside it was already handled above).
    std::map<std::string, int> inDegree;
    std::map<std::string, std::vector<std::string>> dependents;  // dep -> [things depending on it]
    for (const auto& id : eligible) {
        auto& record = records_[id];
        if (record.state != ResourceState::Cached) continue;
        int degree = 0;
        for (const auto& dep : record.entry.dependencies) {
            if (eligibleSet.count(dep) && records_[dep].state == ResourceState::Cached) {
                degree++;
                dependents[dep].push_back(id);
            }
        }
        inDegree[id] = degree;
    }

    std::deque<std::string> queue;
    for (const auto& [id, degree] : inDegree) {
        if (degree == 0) queue.push_back(id);
    }

    std::set<std::string> processed;
    while (!queue.empty()) {
        std::string id = queue.front();
        queue.pop_front();
        processed.insert(id);

        if (LoadOne(id)) {
            summary.loaded++;
        } else {
            summary.failed++;
            summary.errors.emplace_back(id, records_[id].error);
        }

        for (const auto& dependent : dependents[id]) {
            if (--inDegree[dependent] == 0) queue.push_back(dependent);
        }
    }

    // Anything left with inDegree > 0 never got processed: it's part of
    // (or depends on) a cycle.
    for (const auto& [id, degree] : inDegree) {
        if (processed.count(id)) continue;
        TransitionTo(id, ResourceState::Failed, "circular dependency");
        summary.failed++;
        summary.errors.emplace_back(id, "circular dependency");
    }

    return summary;
}

bool ResourceRegistry::Start(const std::string& id, std::string* errorOut) {
    if (!TransitionTo(id, ResourceState::Started)) {
        if (errorOut) *errorOut = "cannot start resource \"" + id + "\" from its current state";
        return false;
    }
    return true;
}

bool ResourceRegistry::Stop(const std::string& id, std::string* errorOut) {
    if (!TransitionTo(id, ResourceState::Stopped)) {
        if (errorOut) *errorOut = "cannot stop resource \"" + id + "\" from its current state";
        return false;
    }
    return true;
}

bool ResourceRegistry::Reload(const std::string& id, std::string* errorOut) {
    auto it = records_.find(id);
    if (it == records_.end()) {
        if (errorOut) *errorOut = "unknown resource \"" + id + "\"";
        return false;
    }
    bool wasStarted = it->second.state == ResourceState::Started;
    if (it->second.state != ResourceState::Started && it->second.state != ResourceState::Stopped) {
        if (errorOut) *errorOut = "cannot reload resource \"" + id + "\" from its current state";
        return false;
    }

    if (!TransitionTo(id, ResourceState::Loading)) {
        if (errorOut) *errorOut = "reload transition failed for \"" + id + "\"";
        return false;
    }
    if (!ValidateAndMarkLoaded(*this, resourcesDir_, id, it->second.entry)) {
        if (errorOut) *errorOut = it->second.error;
        return false;
    }
    if (wasStarted) {
        TransitionTo(id, ResourceState::Started);
    }
    return true;
}

ResourceState ResourceRegistry::GetState(const std::string& id) const {
    auto it = records_.find(id);
    return it != records_.end() ? it->second.state : ResourceState::Pending;
}

std::string ResourceRegistry::GetError(const std::string& id) const {
    auto it = records_.find(id);
    return it != records_.end() ? it->second.error : "";
}

std::vector<ResourceRecord> ResourceRegistry::GetAll() const {
    std::vector<ResourceRecord> all;
    all.reserve(records_.size());
    for (const auto& [id, record] : records_) all.push_back(record);
    return all;
}

bool ResourceRegistry::Contains(const std::string& id) const {
    return records_.count(id) > 0;
}

}

