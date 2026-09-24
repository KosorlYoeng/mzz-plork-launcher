#include "TestFramework.h"
#include "../src/Json.h"

using namespace mzzplork::json;

TEST_CASE(Json_ParsesArraysOfObjects) {
    auto result = Parse(R"({"resources":[{"id":"a","size":1},{"id":"b","size":2}]})");
    ASSERT_TRUE(result.ok);
    auto& resources = result.value.objectValue.at("resources").arrayValue;
    ASSERT_EQ(resources.size(), static_cast<size_t>(2));
    ASSERT_EQ(resources[0].objectValue.at("id").AsString(), std::string("a"));
    ASSERT_EQ(resources[1].objectValue.at("size").AsNumber(), 2.0);
}

TEST_CASE(Json_ParsesEmptyArraysAndObjects) {
    auto result = Parse(R"({"a":[],"b":{}})");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.value.objectValue.at("a").arrayValue.size(), static_cast<size_t>(0));
    ASSERT_EQ(result.value.objectValue.at("b").objectValue.size(), static_cast<size_t>(0));
}

TEST_CASE(Json_StringifyRoundTripsThroughParse) {
    Object obj;
    obj["name"] = Value::MakeString("mzzplork");
    obj["count"] = Value::MakeNumber(3);
    obj["ok"] = Value::MakeBool(true);
    std::string text = StringifyObject(obj);

    auto reparsed = Parse(text);
    ASSERT_TRUE(reparsed.ok);
    ASSERT_EQ(reparsed.value.objectValue.at("name").AsString(), std::string("mzzplork"));
    ASSERT_EQ(reparsed.value.objectValue.at("count").AsNumber(), 3.0);
    ASSERT_TRUE(reparsed.value.objectValue.at("ok").AsBool());
}

TEST_CASE(Json_StringifyEscapesSpecialCharacters) {
    Object obj;
    obj["text"] = Value::MakeString("line1\nline2\"quoted\"");
    std::string text = StringifyObject(obj);

    auto reparsed = ParseObject(text);
    ASSERT_TRUE(reparsed.ok);
    ASSERT_EQ(reparsed.value.at("text").AsString(), std::string("line1\nline2\"quoted\""));
}

TEST_CASE(Json_ParseObjectRejectsANonObjectRoot) {
    auto result = ParseObject("[1,2,3]");
    ASSERT_FALSE(result.ok);
}
