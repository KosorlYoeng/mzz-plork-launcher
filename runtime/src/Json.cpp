#include "Json.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace mzzplork::json {

namespace {

struct Cursor {
    const std::string& text;
    size_t pos = 0;

    bool Eof() const { return pos >= text.size(); }
    char Peek() const { return Eof() ? '\0' : text[pos]; }
    char Next() { return Eof() ? '\0' : text[pos++]; }

    void SkipWhitespace() {
        while (!Eof() && std::isspace(static_cast<unsigned char>(Peek()))) pos++;
    }

    bool Consume(char expected) {
        SkipWhitespace();
        if (Peek() != expected) return false;
        pos++;
        return true;
    }
};

bool ParseStringLiteral(Cursor& c, std::string& out, std::string& error) {
    if (!c.Consume('"')) {
        error = "expected opening quote";
        return false;
    }
    out.clear();
    while (!c.Eof() && c.Peek() != '"') {
        char ch = c.Next();
        if (ch == '\\') {
            char esc = c.Next();
            switch (esc) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (c.pos + 4 > c.text.size()) { error = "truncated \\u escape"; return false; }
                    std::string hex = c.text.substr(c.pos, 4);
                    c.pos += 4;
                    out += static_cast<char>(std::strtol(hex.c_str(), nullptr, 16) & 0xFF);
                    break;
                }
                default:
                    error = "unsupported escape sequence";
                    return false;
            }
        } else {
            out += ch;
        }
    }
    if (!c.Consume('"')) {
        error = "unterminated string";
        return false;
    }
    return true;
}

bool ParseValue(Cursor& c, Value& out, std::string& error);

bool ParseArray(Cursor& c, Value& out, std::string& error) {
    if (!c.Consume('[')) { error = "expected '['"; return false; }
    Array items;
    c.SkipWhitespace();
    if (c.Peek() == ']') {
        c.pos++;
        out = Value::MakeArray(std::move(items));
        return true;
    }
    while (true) {
        Value item;
        if (!ParseValue(c, item, error)) return false;
        items.push_back(std::move(item));
        c.SkipWhitespace();
        if (c.Consume(',')) continue;
        if (c.Consume(']')) break;
        error = "expected ',' or ']' in array";
        return false;
    }
    out = Value::MakeArray(std::move(items));
    return true;
}

bool ParseObjectValue(Cursor& c, Value& out, std::string& error) {
    if (!c.Consume('{')) { error = "expected '{'"; return false; }
    Object fields;
    c.SkipWhitespace();
    if (c.Peek() == '}') {
        c.pos++;
        out = Value::MakeObject(std::move(fields));
        return true;
    }
    while (true) {
        c.SkipWhitespace();
        std::string key;
        if (!ParseStringLiteral(c, key, error)) return false;
        if (!c.Consume(':')) { error = "expected ':' after key \"" + key + "\""; return false; }
        Value value;
        if (!ParseValue(c, value, error)) return false;
        fields[key] = std::move(value);

        c.SkipWhitespace();
        if (c.Consume(',')) continue;
        if (c.Consume('}')) break;
        error = "expected ',' or '}' after value for \"" + key + "\"";
        return false;
    }
    out = Value::MakeObject(std::move(fields));
    return true;
}

bool ParseValue(Cursor& c, Value& out, std::string& error) {
    c.SkipWhitespace();
    char next = c.Peek();
    if (next == '"') {
        std::string s;
        if (!ParseStringLiteral(c, s, error)) return false;
        out = Value::MakeString(s);
        return true;
    }
    if (next == '{') return ParseObjectValue(c, out, error);
    if (next == '[') return ParseArray(c, out, error);
    if (c.text.compare(c.pos, 4, "true") == 0) { c.pos += 4; out = Value::MakeBool(true); return true; }
    if (c.text.compare(c.pos, 5, "false") == 0) { c.pos += 5; out = Value::MakeBool(false); return true; }
    if (c.text.compare(c.pos, 4, "null") == 0) { c.pos += 4; out = Value{}; return true; }

    size_t start = c.pos;
    if (c.Peek() == '-') c.pos++;
    while (!c.Eof() && (std::isdigit(static_cast<unsigned char>(c.Peek())) || c.Peek() == '.' || c.Peek() == 'e' || c.Peek() == 'E' || c.Peek() == '+' || c.Peek() == '-')) {
        c.pos++;
    }
    if (c.pos == start) { error = "expected a value"; return false; }
    std::string numText = c.text.substr(start, c.pos - start);
    char* end = nullptr;
    double num = std::strtod(numText.c_str(), &end);
    if (end != numText.c_str() + numText.size()) { error = "invalid number literal: " + numText; return false; }
    out = Value::MakeNumber(num);
    return true;
}

std::string EscapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default: out += ch;
        }
    }
    return out;
}

void StringifyInto(const Value& value, std::ostringstream& out) {
    switch (value.type) {
        case ValueType::Null: out << "null"; break;
        case ValueType::Bool: out << (value.boolValue ? "true" : "false"); break;
        case ValueType::Number: {
            double d = value.numberValue;
            if (d == static_cast<long long>(d)) {
                out << static_cast<long long>(d);
            } else {
                out << d;
            }
            break;
        }
        case ValueType::String:
            out << '"' << EscapeJsonString(value.stringValue) << '"';
            break;
        case ValueType::Array: {
            out << '[';
            for (size_t i = 0; i < value.arrayValue.size(); i++) {
                if (i) out << ',';
                StringifyInto(value.arrayValue[i], out);
            }
            out << ']';
            break;
        }
        case ValueType::Object: {
            out << '{';
            bool first = true;
            for (const auto& [key, val] : value.objectValue) {
                if (!first) out << ',';
                first = false;
                out << '"' << EscapeJsonString(key) << "\":";
                StringifyInto(val, out);
            }
            out << '}';
            break;
        }
    }
}

}  // namespace

ParseResult Parse(const std::string& text) {
    ParseResult result;
    Cursor c{text, 0};
    if (!ParseValue(c, result.value, result.error)) {
        return result;
    }
    result.ok = true;
    return result;
}

ParseObjectResult ParseObject(const std::string& text) {
    ParseObjectResult result;
    auto parsed = Parse(text);
    if (!parsed.ok) {
        result.error = parsed.error;
        return result;
    }
    if (parsed.value.type != ValueType::Object) {
        result.error = "root value is not an object";
        return result;
    }
    result.ok = true;
    result.value = std::move(parsed.value.objectValue);
    return result;
}

std::string Stringify(const Value& value) {
    std::ostringstream out;
    StringifyInto(value, out);
    return out.str();
}

std::string StringifyObject(const Object& object) {
    return Stringify(Value::MakeObject(object));
}

}  // namespace mzzplork::json
