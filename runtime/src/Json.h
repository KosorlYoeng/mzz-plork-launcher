#pragma once
// A minimal JSON library, scoped to what MzzPlork actually needs: reading
// config files and parsing/writing the client<->server wire protocol
// (see server/src/protocol.js). Supports objects, arrays, strings,
// numbers, bools, and null -- not a general-purpose replacement for a
// full JSON library (no comments, no streaming, no big-number precision
// guarantees), but structurally complete for real nested protocol
// messages like resource_manifest's array of resource objects.
#include <map>
#include <string>
#include <vector>

namespace mzzplork::json {

enum class ValueType { String, Number, Bool, Null, Array, Object };

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

struct Value {
    ValueType type = ValueType::Null;
    std::string stringValue;
    double numberValue = 0;
    bool boolValue = false;
    Array arrayValue;
    Object objectValue;

    static Value MakeString(std::string s) { Value v; v.type = ValueType::String; v.stringValue = std::move(s); return v; }
    static Value MakeNumber(double n) { Value v; v.type = ValueType::Number; v.numberValue = n; return v; }
    static Value MakeBool(bool b) { Value v; v.type = ValueType::Bool; v.boolValue = b; return v; }
    static Value MakeObject(Object o) { Value v; v.type = ValueType::Object; v.objectValue = std::move(o); return v; }
    static Value MakeArray(Array a) { Value v; v.type = ValueType::Array; v.arrayValue = std::move(a); return v; }

    std::string AsString(const std::string& fallback = "") const {
        return type == ValueType::String ? stringValue : fallback;
    }
    double AsNumber(double fallback = 0) const {
        return type == ValueType::Number ? numberValue : fallback;
    }
    bool AsBool(bool fallback = false) const {
        return type == ValueType::Bool ? boolValue : fallback;
    }
    const Object* AsObject() const { return type == ValueType::Object ? &objectValue : nullptr; }
    const Array* AsArray() const { return type == ValueType::Array ? &arrayValue : nullptr; }
};

struct ParseResult {
    bool ok = false;
    Value value;
    std::string error;
};

// Parses any JSON value (object, array, string, number, bool, null).
ParseResult Parse(const std::string& text);

// Convenience for the common case (config files, message payloads that
// are always an object at the top level): parses and requires the root
// to be an object, exposing its fields directly.
struct ParseObjectResult {
    bool ok = false;
    Object value;
    std::string error;
};
ParseObjectResult ParseObject(const std::string& text);

// Serializes a value back to a JSON string (compact, no pretty-printing --
// nothing in this codebase needs it).
std::string Stringify(const Value& value);
std::string StringifyObject(const Object& object);

}
