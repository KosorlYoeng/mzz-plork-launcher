#include "NetworkState.h"

namespace mzzplork {

namespace {
json::Object VectorToJson(const Vector3& v) {
    json::Object o;
    o["x"] = json::Value::MakeNumber(v.x);
    o["y"] = json::Value::MakeNumber(v.y);
    o["z"] = json::Value::MakeNumber(v.z);
    return o;
}
Vector3 VectorFromJson(const json::Object& o) {
    Vector3 v;
    if (auto it = o.find("x"); it != o.end()) v.x = it->second.AsNumber();
    if (auto it = o.find("y"); it != o.end()) v.y = it->second.AsNumber();
    if (auto it = o.find("z"); it != o.end()) v.z = it->second.AsNumber();
    return v;
}
json::Object RotationToJson(const Rotation3& r) {
    json::Object o;
    o["pitch"] = json::Value::MakeNumber(r.pitch);
    o["yaw"] = json::Value::MakeNumber(r.yaw);
    o["roll"] = json::Value::MakeNumber(r.roll);
    return o;
}
Rotation3 RotationFromJson(const json::Object& o) {
    Rotation3 r;
    if (auto it = o.find("pitch"); it != o.end()) r.pitch = it->second.AsNumber();
    if (auto it = o.find("yaw"); it != o.end()) r.yaw = it->second.AsNumber();
    if (auto it = o.find("roll"); it != o.end()) r.roll = it->second.AsNumber();
    return r;
}
}  // namespace

json::Object ToJson(const NetworkState& state) {
    json::Object o;
    o["position"] = json::Value::MakeObject(VectorToJson(state.position));
    o["rotation"] = json::Value::MakeObject(RotationToJson(state.rotation));
    o["basicState"] = json::Value::MakeObject(state.basicState);
    o["timestamp"] = json::Value::MakeNumber(static_cast<double>(state.timestampMs));
    return o;
}

NetworkState FromJson(const json::Object& obj) {
    NetworkState state;
    if (auto it = obj.find("position"); it != obj.end() && it->second.type == json::ValueType::Object) {
        state.position = VectorFromJson(it->second.objectValue);
    }
    if (auto it = obj.find("rotation"); it != obj.end() && it->second.type == json::ValueType::Object) {
        state.rotation = RotationFromJson(it->second.objectValue);
    }
    if (auto it = obj.find("basicState"); it != obj.end() && it->second.type == json::ValueType::Object) {
        state.basicState = it->second.objectValue;
    }
    if (auto it = obj.find("timestamp"); it != obj.end()) {
        state.timestampMs = static_cast<int64_t>(it->second.AsNumber());
    }
    return state;
}

}
