#include <fstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/ResourceRegistry.h"

using namespace mzzplork;

namespace {

ResourceEntry Entry(const std::string& id, long long size = 5, std::vector<std::string> deps = {}) {
    ResourceEntry e;
    e.id = id;
    e.path = id;
    e.size = size;
    e.hash = "";  // not checked by LoadOne's freshness check unless size > 0 mismatches
    e.version = "1.0.0";
    e.dependencies = std::move(deps);
    return e;
}

void WriteFile(const std::string& dir, const std::string& name, size_t size) {
    std::ofstream(dir + "\\" + name, std::ios::binary) << std::string(size, 'x');
}

}  // namespace

TEST_CASE(ResourceRegistry_LoadManifestStartsEveryEntryPending) {
    std::string dir = mzzplork::test::MakeTempAppData();
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("a"), Entry("b")});

    ASSERT_TRUE(registry.GetState("a") == ResourceState::Pending);
    ASSERT_TRUE(registry.GetState("b") == ResourceState::Pending);
    ASSERT_EQ(registry.GetAll().size(), static_cast<size_t>(2));
}

TEST_CASE(ResourceRegistry_TransitionToRejectsInvalidJumps) {
    std::string dir = mzzplork::test::MakeTempAppData();
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("a")});

    ASSERT_FALSE(registry.TransitionTo("a", ResourceState::Loading));  // Pending -> Loading is not valid
    ASSERT_TRUE(registry.GetState("a") == ResourceState::Pending);     // state unchanged
    ASSERT_TRUE(registry.TransitionTo("a", ResourceState::Cached));    // the cache-hit shortcut is valid
}

// "dependency": a simple two-node chain loads in the correct order and
// both end up Loaded.
TEST_CASE(ResourceRegistry_LoadsADependencyBeforeItsDependent) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "base.txt", 5);
    WriteFile(dir, "dependent.txt", 5);

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("base.txt"), Entry("dependent.txt", 5, {"base.txt"})});
    registry.TransitionTo("base.txt", ResourceState::Cached);
    registry.TransitionTo("dependent.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.loaded, 2);
    ASSERT_EQ(summary.failed, 0);
    ASSERT_TRUE(registry.GetState("base.txt") == ResourceState::Loaded);
    ASSERT_TRUE(registry.GetState("dependent.txt") == ResourceState::Loaded);
}

TEST_CASE(ResourceRegistry_MissingDependencyFailsOnlyTheDependent) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "dependent.txt", 5);

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("dependent.txt", 5, {"does-not-exist.txt"})});
    registry.TransitionTo("dependent.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.loaded, 0);
    ASSERT_EQ(summary.failed, 1);
    ASSERT_TRUE(registry.GetState("dependent.txt") == ResourceState::Failed);
    ASSERT_TRUE(registry.GetError("dependent.txt").find("missing dependency") != std::string::npos);
}

TEST_CASE(ResourceRegistry_ADependencyThatFailedToDownloadCascadesToItsDependents) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "dependent.txt", 5);
    // "base.txt" deliberately never gets a file written -- it failed to download.

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("base.txt"), Entry("dependent.txt", 5, {"base.txt"})});
    registry.TransitionTo("base.txt", ResourceState::Downloading);
    registry.TransitionTo("base.txt", ResourceState::Failed, "download failed");
    registry.TransitionTo("dependent.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.failed, 1);  // only dependent.txt counted -- base.txt failed before LoadAll ran
    ASSERT_TRUE(registry.GetState("dependent.txt") == ResourceState::Failed);
    ASSERT_TRUE(registry.GetError("dependent.txt").find("dependency failed") != std::string::npos);
}

TEST_CASE(ResourceRegistry_ACircularDependencyFailsEveryResourceInTheCycle) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "a.txt", 5);
    WriteFile(dir, "b.txt", 5);

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("a.txt", 5, {"b.txt"}), Entry("b.txt", 5, {"a.txt"})});
    registry.TransitionTo("a.txt", ResourceState::Cached);
    registry.TransitionTo("b.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.loaded, 0);
    ASSERT_EQ(summary.failed, 2);
    ASSERT_TRUE(registry.GetState("a.txt") == ResourceState::Failed);
    ASSERT_TRUE(registry.GetState("b.txt") == ResourceState::Failed);
    ASSERT_TRUE(registry.GetError("a.txt").find("circular") != std::string::npos);
}

TEST_CASE(ResourceRegistry_ResourcesOutsideAFailedSubgraphStillLoad) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "independent.txt", 5);
    WriteFile(dir, "dependent.txt", 5);

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("independent.txt"), Entry("dependent.txt", 5, {"missing.txt"})});
    registry.TransitionTo("independent.txt", ResourceState::Cached);
    registry.TransitionTo("dependent.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_TRUE(registry.GetState("independent.txt") == ResourceState::Loaded);
    ASSERT_TRUE(registry.GetState("dependent.txt") == ResourceState::Failed);
    ASSERT_EQ(summary.loaded, 1);
    ASSERT_EQ(summary.failed, 1);
}

// "Do not allow unverified files to load": a resource still Pending (or
// Downloading, or Failed) is simply not eligible for LoadAll() at all --
// not silently skipped-but-still-counted, genuinely never touched.
TEST_CASE(ResourceRegistry_UnverifiedResourcesAreNeverLoaded) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "pending.txt", 5);

    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("pending.txt")});
    // Deliberately left at Pending -- never transitioned through
    // Downloading/Verifying/Cached, i.e. never actually verified.

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.loaded, 0);
    ASSERT_EQ(summary.failed, 0);
    ASSERT_TRUE(registry.GetState("pending.txt") == ResourceState::Pending);
}

TEST_CASE(ResourceRegistry_LoadOneFailsIfTheFileDisappearedAfterVerification) {
    std::string dir = mzzplork::test::MakeTempAppData();
    // No file written for "ghost.txt" -- simulates external deletion
    // between verification and load.
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("ghost.txt")});
    registry.TransitionTo("ghost.txt", ResourceState::Cached);

    auto summary = registry.LoadAll();

    ASSERT_EQ(summary.failed, 1);
    ASSERT_TRUE(registry.GetState("ghost.txt") == ResourceState::Failed);
}

TEST_CASE(ResourceRegistry_StartStopReloadLifecycle) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "res.txt", 5);
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("res.txt")});
    registry.TransitionTo("res.txt", ResourceState::Cached);
    registry.LoadAll();
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Loaded);

    ASSERT_TRUE(registry.Start("res.txt"));
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Started);

    ASSERT_TRUE(registry.Stop("res.txt"));
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Stopped);

    ASSERT_TRUE(registry.Start("res.txt"));
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Started);

    ASSERT_TRUE(registry.Reload("res.txt"));
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Started);  // reload restores the running state
}

TEST_CASE(ResourceRegistry_StartFailsFromAnIneligibleState) {
    std::string dir = mzzplork::test::MakeTempAppData();
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("res.txt")});
    // still Pending -- never loaded

    std::string error;
    ASSERT_FALSE(registry.Start("res.txt", &error));
    ASSERT_FALSE(error.empty());
}

TEST_CASE(ResourceRegistry_ReloadDetectsAFileThatChangedUnderneathIt) {
    std::string dir = mzzplork::test::MakeTempAppData();
    WriteFile(dir, "res.txt", 5);
    ResourceRegistry registry(dir);
    registry.LoadManifest({Entry("res.txt", 5)});
    registry.TransitionTo("res.txt", ResourceState::Cached);
    registry.LoadAll();
    registry.Start("res.txt");

    // Simulate the file changing size outside the resource manager.
    std::ofstream(dir + "\\res.txt", std::ios::binary) << "not five bytes anymore";

    std::string error;
    ASSERT_FALSE(registry.Reload("res.txt", &error));
    ASSERT_TRUE(registry.GetState("res.txt") == ResourceState::Failed);
}
