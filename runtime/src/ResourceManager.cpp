#include "ResourceManager.h"
#include "HttpDownloader.h"
#include "Sha256.h"

#include <filesystem>

namespace mzzplork {

namespace fs = std::filesystem;

namespace {

std::vector<ResourceEntry> ParseResourceList(const json::Object& manifestPayload) {
    std::vector<ResourceEntry> entries;
    auto it = manifestPayload.find("resources");
    if (it == manifestPayload.end() || it->second.type != json::ValueType::Array) return entries;
    for (const auto& item : it->second.arrayValue) {
        if (item.type != json::ValueType::Object) continue;
        ResourceEntry entry;
        entry.id = item.objectValue.count("id") ? item.objectValue.at("id").AsString() : "";
        entry.path = item.objectValue.count("path") ? item.objectValue.at("path").AsString() : entry.id;
        entry.size = item.objectValue.count("size") ? static_cast<long long>(item.objectValue.at("size").AsNumber()) : 0;
        entry.hash = item.objectValue.count("hash") ? item.objectValue.at("hash").AsString() : "";
        entry.version = item.objectValue.count("version") ? item.objectValue.at("version").AsString() : "";
        if (auto depsIt = item.objectValue.find("dependencies"); depsIt != item.objectValue.end() && depsIt->second.type == json::ValueType::Array) {
            for (const auto& dep : depsIt->second.arrayValue) {
                if (dep.type == json::ValueType::String) entry.dependencies.push_back(dep.stringValue);
            }
        }
        if (!entry.id.empty()) entries.push_back(entry);
    }
    return entries;
}

bool LocalFileMatches(const std::string& filePath, const ResourceEntry& entry) {
    std::error_code ec;
    if (!fs::exists(filePath, ec)) return false;
    auto size = static_cast<long long>(fs::file_size(filePath, ec));
    if (ec || size != entry.size) return false;
    try {
        return Sha256HexOfFile(filePath) == entry.hash;
    } catch (...) {
        return false;
    }
}

class FileResourceManager : public IResourceManager {
public:
    ServiceResult Initialize(const std::string& appDataRoot) override {
        appDataRoot_ = appDataRoot;
        resourcesDir_ = appDataRoot_ + "\\resources";
        std::error_code ec;
        fs::create_directories(resourcesDir_, ec);
        if (ec) {
            return ServiceResult::Failed("could not create resources directory: " + ec.message());
        }
        registry_ = std::make_unique<ResourceRegistry>(resourcesDir_);
        initialized_ = true;
        return ServiceResult::Ok("resource manager ready");
    }

    bool IsInitialized() const override { return initialized_; }

    ServiceResult ApplyManifest(const json::Object& manifestPayload, const std::string& serverHost) override {
        if (!initialized_) return ServiceResult::Failed("resource manager not initialized");

        auto portIt = manifestPayload.find("httpPort");
        unsigned short httpPort = portIt != manifestPayload.end() ? static_cast<unsigned short>(portIt->second.AsNumber(0)) : 0;
        if (httpPort == 0) {
            return ServiceResult::Failed("manifest did not include a valid httpPort");
        }

        auto entries = ParseResourceList(manifestPayload);
        registry_->LoadManifest(entries);

        int downloaded = 0, unchanged = 0, failed = 0;
        for (const auto& entry : entries) {
            std::string targetPath = resourcesDir_ + "\\" + entry.path;

            if (LocalFileMatches(targetPath, entry)) {
                registry_->TransitionTo(entry.id, ResourceState::Cached);
                unchanged++;
                continue;
            }

            registry_->TransitionTo(entry.id, ResourceState::Downloading);

            DownloadRequest request;
            request.host = serverHost;
            request.port = httpPort;
            request.urlPath = "/resources/" + entry.id;
            request.destPath = targetPath;
            request.expectedSize = entry.size;
            request.expectedHashHex = entry.hash;

            DownloadResult result = DownloadAndVerify(request);
            if (!result.ok) {
                registry_->TransitionTo(entry.id, ResourceState::Failed, result.error);
                failed++;
                continue;
            }

            registry_->TransitionTo(entry.id, ResourceState::Verifying);
            registry_->TransitionTo(entry.id, ResourceState::Cached);
            downloaded++;
        }

        // Every resource is processed independently -- one failing does
        // not stop the others from downloading, and does not fail this
        // call overall. Per-resource outcomes (and error messages) are
        // available via GetAllResources()/GetResourceState(); this
        // result is about whether manifest processing itself ran, not
        // whether every resource succeeded.
        return ServiceResult::Ok(
            "resources: " + std::to_string(downloaded) + " downloaded, " + std::to_string(unchanged) + " unchanged, "
            + std::to_string(failed) + " failed, " + std::to_string(entries.size()) + " total");
    }

    ResourceRegistry::LoadSummary LoadAll() override {
        return registry_->LoadAll();
    }

    bool StartResource(const std::string& id, std::string* errorOut) override {
        return registry_->Start(id, errorOut);
    }
    bool StopResource(const std::string& id, std::string* errorOut) override {
        return registry_->Stop(id, errorOut);
    }
    bool ReloadResource(const std::string& id, std::string* errorOut) override {
        return registry_->Reload(id, errorOut);
    }

    ResourceState GetResourceState(const std::string& id) const override {
        return registry_->GetState(id);
    }
    std::vector<ResourceRecord> GetAllResources() const override {
        return registry_->GetAll();
    }

private:
    std::string appDataRoot_;
    std::string resourcesDir_;
    bool initialized_ = false;
    std::unique_ptr<ResourceRegistry> registry_;
};

}  // namespace

std::unique_ptr<IResourceManager> MakeFileResourceManager() {
    return std::make_unique<FileResourceManager>();
}

}
