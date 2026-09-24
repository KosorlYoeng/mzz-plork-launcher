#include "TestFramework.h"
#include "../src/EventSystem.h"

using mzzplork::EventSystem;

TEST_CASE(EventSystem_PublishInvokesMatchingSubscriber) {
    EventSystem events;
    bool invoked = false;
    mzzplork::json::Object receivedData;
    events.Subscribe("server_message", [&](const mzzplork::json::Object& data) {
        invoked = true;
        receivedData = data;
    });

    mzzplork::json::Object payload;
    payload["message"] = mzzplork::json::Value::MakeString("hello");
    events.Publish("server_message", payload);

    ASSERT_TRUE(invoked);
    ASSERT_EQ(receivedData.at("message").AsString(), std::string("hello"));
}

TEST_CASE(EventSystem_PublishDoesNotInvokeSubscribersForOtherTypes) {
    EventSystem events;
    bool invoked = false;
    events.Subscribe("player_joined", [&](const mzzplork::json::Object&) { invoked = true; });

    events.Publish("server_message", {});

    ASSERT_FALSE(invoked);
}

TEST_CASE(EventSystem_MultipleSubscribersToTheSameTypeAllRun) {
    EventSystem events;
    int count = 0;
    events.Subscribe("tick", [&](const mzzplork::json::Object&) { count++; });
    events.Subscribe("tick", [&](const mzzplork::json::Object&) { count++; });

    events.Publish("tick", {});

    ASSERT_EQ(count, 2);
}
