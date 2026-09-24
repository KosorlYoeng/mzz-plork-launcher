#include <fstream>

#include "TestFramework.h"
#include "TestUtil.h"
#include "../src/Config.h"
#include "../src/Json.h"

using mzzplork::LoadConfig;
using mzzplork::ServiceStatus;

TEST_CASE(Json_ParsesAFlatObjectOfMixedTypes) {
    auto result = mzzplork::json::ParseObject(R"({"name":"mzzplork","port":7777,"debug":true,"empty":null})");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.value["name"].AsString(), std::string("mzzplork"));
    ASSERT_EQ(result.value["port"].AsNumber(), 7777.0);
    ASSERT_TRUE(result.value["debug"].AsBool() == true);
}

TEST_CASE(Json_ReportsAnErrorForMalformedInput) {
    auto result = mzzplork::json::ParseObject("{not valid json");
    ASSERT_FALSE(result.ok);
    ASSERT_FALSE(result.error.empty());
}

TEST_CASE(Config_UsesDefaultsWhenNoConfigFileExists) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    auto loaded = LoadConfig(appDataRoot);
    ASSERT_TRUE(loaded.result.status == ServiceStatus::Ok);
    ASSERT_EQ(loaded.config.logLevel, std::string("info"));
}

TEST_CASE(Config_LoadsRealValuesFromConfigJson) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::ofstream configFile(appDataRoot + "\\config\\config.json");
    configFile << R"({"logLevel":"debug","serverHost":"127.0.0.1","serverPort":7777,"gtaInstallPathOverride":"C:\\GTA5.exe"})";
    configFile.close();

    auto loaded = LoadConfig(appDataRoot);
    ASSERT_TRUE(loaded.result.status == ServiceStatus::Ok);
    ASSERT_EQ(loaded.config.logLevel, std::string("debug"));
    ASSERT_EQ(loaded.config.serverHost, std::string("127.0.0.1"));
    ASSERT_EQ(loaded.config.serverPort, 7777);
    ASSERT_EQ(loaded.config.gtaInstallPathOverride, std::string("C:\\GTA5.exe"));
}

TEST_CASE(Config_FailsCleanlyOnMalformedConfigJson) {
    std::string appDataRoot = mzzplork::test::MakeTempAppData();
    std::ofstream configFile(appDataRoot + "\\config\\config.json");
    configFile << "{not valid json";
    configFile.close();

    auto loaded = LoadConfig(appDataRoot);
    ASSERT_TRUE(loaded.result.status == ServiceStatus::Failed);
    ASSERT_FALSE(loaded.result.message.empty());
}

