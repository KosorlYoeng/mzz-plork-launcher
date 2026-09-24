#include <filesystem>
#include <fstream>
#include <sstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "MinimalHttpServer.h"
#include "../src/ResourceManager.h"
#include "../src/Sha256.h"

using namespace mzzplork;

// Network-dependent paths (actually downloading a changed/missing
// resource) are covered by the real end-to-end test against the real
// server (E2ETests.cpp) rather than duplicated here with a fake HTTP
// server -- these tests cover what's reachable without one: setup,
// input validation, and the "already correct locally" skip path, which
// only succeeds if ApplyManifest() never attempts a network call at all
// (there is no server listening in this test, so an attempted download
// would fail).

TEST_CASE(ResourceManager_InitializeCreatesTheResourcesDirectory) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    auto result = manager->Initialize(appDataRoot);

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_TRUE(std::filesystem::exists(appDataRoot + "\\resources"));
}

TEST_CASE(ResourceManager_ApplyManifestFailsCleanlyWithoutAValidHttpPort) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);

    json::Object manifest;
    manifest["resources"] = json::Value::MakeArray({});
    // no httpPort field at all

    auto result = manager->ApplyManifest(manifest, "127.0.0.1");
    ASSERT_TRUE(result.status == ServiceStatus::Failed);
}

TEST_CASE(ResourceManager_SkipsDownloadingAFileThatAlreadyMatchesLocally) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);

    std::string content = "already-correct-content";
    std::filesystem::create_directories(appDataRoot + "\\resources");
    std::ofstream(appDataRoot + "\\resources\\existing.txt", std::ios::binary) << content;

    json::Object fileEntry;
    fileEntry["id"] = json::Value::MakeString("existing.txt");
    fileEntry["path"] = json::Value::MakeString("existing.txt");
    fileEntry["size"] = json::Value::MakeNumber(static_cast<double>(content.size()));
    fileEntry["hash"] = json::Value::MakeString(Sha256Hex(content));
    fileEntry["version"] = json::Value::MakeString("1.0.0");

    json::Object manifest;
    manifest["httpPort"] = json::Value::MakeNumber(1);  // deliberately unreachable -- must never be dialed
    manifest["resources"] = json::Value::MakeArray({json::Value::MakeObject(fileEntry)});

    // No server is listening anywhere in this test process. If
    // ApplyManifest tried to download "existing.txt" instead of
    // recognizing it as already correct, this would fail/time out rather
    // than succeed quickly.
    auto result = manager->ApplyManifest(manifest, "127.0.0.1");
    ASSERT_TRUE(result.status == ServiceStatus::Ok);
}

// --- Tests below use a real local HTTP server (MinimalHttpServer, built
// for HttpDownloaderTests.cpp) rather than mocking the network, matching
// this project's established testing philosophy. ---

namespace {

json::Object ManifestWith(unsigned short httpPort, const std::vector<json::Object>& fileEntries) {
    std::vector<json::Value> arr;
    for (auto& f : fileEntries) arr.push_back(json::Value::MakeObject(f));
    json::Object manifest;
    manifest["httpPort"] = json::Value::MakeNumber(httpPort);
    manifest["resources"] = json::Value::MakeArray(arr);
    return manifest;
}

json::Object FileEntry(const std::string& id, const std::string& content, std::vector<std::string> deps = {}) {
    json::Object e;
    e["id"] = json::Value::MakeString(id);
    e["path"] = json::Value::MakeString(id);
    e["size"] = json::Value::MakeNumber(static_cast<double>(content.size()));
    e["hash"] = json::Value::MakeString(Sha256Hex(content));
    e["version"] = json::Value::MakeString("1.0.0");
    if (!deps.empty()) {
        std::vector<json::Value> depVals;
        for (auto& d : deps) depVals.push_back(json::Value::MakeString(d));
        e["dependencies"] = json::Value::MakeArray(depVals);
    }
    return e;
}

}  // namespace

// missing resource
TEST_CASE(ResourceManager_MissingResourceIsDownloadedAndCached) {
    std::string content = "brand-new-resource";
    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest&) {
        mzzplork::test::TestHttpResponse resp;
        resp.body = content;
        return resp;
    });

    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);

    auto manifest = ManifestWith(server.Port(), {FileEntry("new.txt", content)});
    auto result = manager->ApplyManifest(manifest, "127.0.0.1");

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_TRUE(manager->GetResourceState("new.txt") == ResourceState::Cached);
}

// changed resource
TEST_CASE(ResourceManager_ChangedResourceIsRedownloaded) {
    std::string oldContent = "version-one";
    std::string newContent = "version-two-different-length";
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::filesystem::create_directories(appDataRoot + "\\resources");
    std::ofstream(appDataRoot + "\\resources\\changed.txt", std::ios::binary) << oldContent;

    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest&) {
        mzzplork::test::TestHttpResponse resp;
        resp.body = newContent;
        return resp;
    });

    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);
    auto manifest = ManifestWith(server.Port(), {FileEntry("changed.txt", newContent)});
    auto result = manager->ApplyManifest(manifest, "127.0.0.1");

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_EQ(server.RequestCount(), 1);
    ASSERT_TRUE(manager->GetResourceState("changed.txt") == ResourceState::Cached);
    std::ifstream in(appDataRoot + "\\resources\\changed.txt", std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    ASSERT_EQ(buf.str(), newContent);
}

// corrupted resource
TEST_CASE(ResourceManager_CorruptedResourceIsMarkedFailed) {
    std::string promised = "what-the-manifest-promises";
    std::string actuallySent = "something-completely-different";
    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest&) {
        mzzplork::test::TestHttpResponse resp;
        resp.body = actuallySent;
        return resp;
    });

    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);
    auto manifest = ManifestWith(server.Port(), {FileEntry("corrupt.txt", promised)});
    manager->ApplyManifest(manifest, "127.0.0.1");

    ASSERT_TRUE(manager->GetResourceState("corrupt.txt") == ResourceState::Failed);
    ASSERT_FALSE(std::filesystem::exists(appDataRoot + "\\resources\\corrupt.txt"));
}

// failed download
TEST_CASE(ResourceManager_FailedDownloadIsIsolatedToThatResource) {
    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest& req) {
        mzzplork::test::TestHttpResponse resp;
        if (req.path.find("missing.txt") != std::string::npos) {
            resp.statusCode = 404;
            return resp;
        }
        resp.body = "fine.txt content";
        return resp;
    });

    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);
    auto manifest = ManifestWith(server.Port(), {FileEntry("missing.txt", "whatever"), FileEntry("fine.txt", "fine.txt content")});
    auto result = manager->ApplyManifest(manifest, "127.0.0.1");

    ASSERT_TRUE(result.status == ServiceStatus::Ok);  // manifest processing itself succeeded
    ASSERT_TRUE(manager->GetResourceState("missing.txt") == ResourceState::Failed);
    ASSERT_TRUE(manager->GetResourceState("fine.txt") == ResourceState::Cached);  // unaffected by the other's failure
}

// cache hit
TEST_CASE(ResourceManager_CacheHitNeverTouchesTheNetwork) {
    std::string content = "unchanged-content";
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::filesystem::create_directories(appDataRoot + "\\resources");
    std::ofstream(appDataRoot + "\\resources\\cached.txt", std::ios::binary) << content;

    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest&) {
        mzzplork::test::TestHttpResponse resp;
        resp.statusCode = 500;  // must never be hit
        return resp;
    });

    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);
    auto manifest = ManifestWith(server.Port(), {FileEntry("cached.txt", content)});
    auto result = manager->ApplyManifest(manifest, "127.0.0.1");

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_EQ(server.RequestCount(), 0);
    ASSERT_TRUE(manager->GetResourceState("cached.txt") == ResourceState::Cached);
}

// cache invalidation
TEST_CASE(ResourceManager_ApplyingANewManifestInvalidatesTheOldCacheEntry) {
    std::string contentA = "first-manifest-content";
    std::string contentB = "second-manifest-content-different";
    std::string appDataRoot = mzzplork::test::MakeTempAppData();

    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest&) {
        mzzplork::test::TestHttpResponse resp;
        resp.body = contentB;
        return resp;
    });

    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);

    // First manifest: resource already present and matching -- cache hit.
    std::filesystem::create_directories(appDataRoot + "\\resources");
    std::ofstream(appDataRoot + "\\resources\\evolving.txt", std::ios::binary) << contentA;
    auto firstManifest = ManifestWith(server.Port(), {FileEntry("evolving.txt", contentA)});
    manager->ApplyManifest(firstManifest, "127.0.0.1");
    ASSERT_EQ(server.RequestCount(), 0);
    ASSERT_TRUE(manager->GetResourceState("evolving.txt") == ResourceState::Cached);

    // Second manifest: the server now promises different content --
    // the stale Cached state from the first call must not be trusted.
    auto secondManifest = ManifestWith(server.Port(), {FileEntry("evolving.txt", contentB)});
    auto result = manager->ApplyManifest(secondManifest, "127.0.0.1");

    ASSERT_TRUE(result.status == ServiceStatus::Ok);
    ASSERT_EQ(server.RequestCount(), 1);
    ASSERT_TRUE(manager->GetResourceState("evolving.txt") == ResourceState::Cached);
    std::ifstream in(appDataRoot + "\\resources\\evolving.txt", std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    ASSERT_EQ(buf.str(), contentB);
}

// dependency, parsed end-to-end from a real manifest through to LoadAll()
TEST_CASE(ResourceManager_DependenciesParsedFromTheManifestAreRespectedByLoadAll) {
    std::string baseContent = "base";
    std::string dependentContent = "dependent";
    mzzplork::test::MinimalHttpServer server([&](const mzzplork::test::TestHttpRequest& req) {
        mzzplork::test::TestHttpResponse resp;
        resp.body = req.path.find("base.txt") != std::string::npos ? baseContent : dependentContent;
        return resp;
    });

    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto manager = MakeFileResourceManager();
    manager->Initialize(appDataRoot);
    auto manifest = ManifestWith(server.Port(), {
        FileEntry("dependent.txt", dependentContent, {"base.txt"}),
        FileEntry("base.txt", baseContent)
    });
    manager->ApplyManifest(manifest, "127.0.0.1");

    auto summary = manager->LoadAll();

    ASSERT_EQ(summary.loaded, 2);
    ASSERT_EQ(summary.failed, 0);
    ASSERT_TRUE(manager->GetResourceState("base.txt") == ResourceState::Loaded);
    ASSERT_TRUE(manager->GetResourceState("dependent.txt") == ResourceState::Loaded);
}
