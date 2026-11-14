#pragma once
// Strict JSON (RFC 8259) for untrusted input: the annotation store files and the MCP bridge's stdin.
// Header-only, no dependency beyond the C++17 standard library.
//
// Strictness: one value, optional surrounding whitespace, nothing else (no comments, no trailing commas,
// no NaN/Infinity, no leading zeros, no raw control characters in strings). Strings must be valid UTF-8
// (no overlong forms, no encoded surrogates, nothing above U+10FFFF); \u escapes must pair surrogates.
// Duplicate object keys are rejected. Input size, nesting depth, string size and item counts are bounded
// (Limits), so a hostile document cannot exhaust the stack or memory. A UTF-8 BOM is refused unless allowed.
//
// Serialize() emits compact JSON on a single line: every control character, U+2028/U+2029, '"' and '\' are
// escaped, so the output can be framed by newlines (MCP stdio) and embedded safely.
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace StrictJson {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value {
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    bool integral = false;    // number written without fraction/exponent and fits in int64
    std::int64_t integer = 0;  // valid when integral
    std::string string;
    std::vector<Value> items;                            // Array
    std::vector<std::pair<std::string, Value>> members;  // Object, in document order

    Value() = default;
    static Value Null() { return Value(); }
    static Value Bool(bool b) { Value v; v.type = Type::Bool; v.boolean = b; return v; }
    static Value Int(std::int64_t i) {
        Value v; v.type = Type::Number; v.integral = true; v.integer = i; v.number = double(i); return v;
    }
    static Value Num(double d) {
        Value v; v.type = Type::Number; v.number = std::isfinite(d) ? d : 0;
        if (v.number == std::floor(v.number) && std::fabs(v.number) < 9.0e15) { v.integral = true; v.integer = std::int64_t(v.number); }
        return v;
    }
    static Value Str(std::string s) { Value v; v.type = Type::String; v.string = std::move(s); return v; }
    static Value Arr() { Value v; v.type = Type::Array; return v; }
    static Value Obj() { Value v; v.type = Type::Object; return v; }

    bool IsNull() const { return type == Type::Null; }
    bool IsBool() const { return type == Type::Bool; }
    bool IsNumber() const { return type == Type::Number; }
    bool IsString() const { return type == Type::String; }
    bool IsArray() const { return type == Type::Array; }
    bool IsObject() const { return type == Type::Object; }

    // Object access (nullptr when absent or not an object).
    const Value* Get(std::string_view key) const {
        if (type != Type::Object) return nullptr;
        for (const auto& m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }
    // Builder: replaces an existing key, otherwise appends (keeps insertion order, deterministic output).
    Value& Set(std::string key, Value v) {
        type = Type::Object;
        for (auto& m : members)
            if (m.first == key) { m.second = std::move(v); return m.second; }
        members.emplace_back(std::move(key), std::move(v));
        return members.back().second;
    }
    Value& Push(Value v) {
        type = Type::Array;
        items.push_back(std::move(v));
        return items.back();
    }
};

struct Limits {
    std::size_t maxBytes = 1u << 20;        // whole document
    std::size_t maxDepth = 32;              // arrays/objects nesting
    std::size_t maxStringBytes = 1u << 20;  // one decoded string (keys included)
    std::size_t maxItems = 1u << 16;        // total array items + object members in the document
    bool allowBom = false;
};

namespace detail {

inline bool AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F));
    } else return false;
    return true;
}

// Decodes one UTF-8 sequence at s[i]; returns its length (0 = invalid). Strict per RFC 3629.
inline std::size_t Utf8Length(std::string_view s, std::size_t i, std::uint32_t* cpOut = nullptr) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp;
    std::size_t len;
    if (c < 0x80) { if (cpOut) *cpOut = c; return 1; }
    if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1F; len = 2; }
    else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0F; len = 3; }
    else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07; len = 4; }
    else return 0;
    if (i + len > s.size()) return 0;
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (d & 0x3F);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    if (cpOut) *cpOut = cp;
    return len;
}

class Parser {
public:
    Parser(std::string_view text, const Limits& limits) : s_(text), lim_(limits) {}

    bool Run(Value& out, std::string* error) {
        if (s_.size() > lim_.maxBytes) return Fail("document too large", error);
        if (s_.size() >= 3 && static_cast<unsigned char>(s_[0]) == 0xEF && static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            if (!lim_.allowBom) return Fail("byte order mark", error);
            p_ = 3;
        }
        SkipWs();
        if (!ParseValue(out, 0)) return Fail(err_, error);
        SkipWs();
        if (p_ != s_.size()) return Fail("trailing characters", error);
        return true;
    }

private:
    std::string_view s_;
    Limits lim_;
    std::size_t p_ = 0, items_ = 0;
    const char* err_ = "invalid";

    bool Fail(const char* what, std::string* error) {
        if (error) *error = std::string(what) + " at byte " + std::to_string(p_);
        return false;
    }
    bool Bad(const char* what) { err_ = what; return false; }
    void SkipWs() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
    }
    bool Literal(const char* word) {
        const std::size_t n = std::strlen(word);
        if (s_.compare(p_, n, word) != 0) return Bad("invalid literal");
        p_ += n;
        return true;
    }
    bool ParseValue(Value& v, std::size_t depth) {
        if (p_ >= s_.size()) return Bad("unexpected end");
        switch (s_[p_]) {
        case 'n': v = Value(); return Literal("null");
        case 't': v = Value::Bool(true); return Literal("true");
        case 'f': v = Value::Bool(false); return Literal("false");
        case '"': v = Value(); v.type = Type::String; return ParseString(v.string);
        case '[': return ParseArray(v, depth + 1);
        case '{': return ParseObject(v, depth + 1);
        default: return ParseNumber(v);
        }
    }
    bool ParseArray(Value& v, std::size_t depth) {
        if (depth > lim_.maxDepth) return Bad("nesting too deep");
        v = Value::Arr();
        ++p_;
        SkipWs();
        if (p_ < s_.size() && s_[p_] == ']') { ++p_; return true; }
        for (;;) {
            if (++items_ > lim_.maxItems) return Bad("too many items");
            v.items.emplace_back();
            SkipWs();
            if (!ParseValue(v.items.back(), depth)) return false;
            SkipWs();
            if (p_ >= s_.size()) return Bad("unexpected end");
            if (s_[p_] == ',') { ++p_; continue; }
            if (s_[p_] == ']') { ++p_; return true; }
            return Bad("expected , or ]");
        }
    }
    bool ParseObject(Value& v, std::size_t depth) {
        if (depth > lim_.maxDepth) return Bad("nesting too deep");
        v = Value::Obj();
        ++p_;
        SkipWs();
        if (p_ < s_.size() && s_[p_] == '}') { ++p_; return true; }
        for (;;) {
            if (++items_ > lim_.maxItems) return Bad("too many items");
            SkipWs();
            if (p_ >= s_.size() || s_[p_] != '"') return Bad("expected key");
            std::string key;
            if (!ParseString(key)) return false;
            for (const auto& m : v.members)
                if (m.first == key) return Bad("duplicate key");
            SkipWs();
            if (p_ >= s_.size() || s_[p_] != ':') return Bad("expected :");
            ++p_;
            SkipWs();
            v.members.emplace_back(std::move(key), Value());
            if (!ParseValue(v.members.back().second, depth)) return false;
            SkipWs();
            if (p_ >= s_.size()) return Bad("unexpected end");
            if (s_[p_] == ',') { ++p_; continue; }
            if (s_[p_] == '}') { ++p_; return true; }
            return Bad("expected , or }");
        }
    }
    bool Hex4(std::uint32_t& out) {
        if (p_ + 4 > s_.size()) return Bad("short escape");
        out = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[p_++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= std::uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') out |= std::uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= std::uint32_t(c - 'A' + 10);
            else return Bad("bad hex escape");
        }
        return true;
    }
    bool ParseString(std::string& out) {
        ++p_;  // opening quote
        out.clear();
        for (;;) {
            if (p_ >= s_.size()) return Bad("unterminated string");
            if (out.size() > lim_.maxStringBytes) return Bad("string too long");
            const unsigned char c = static_cast<unsigned char>(s_[p_]);
            if (c == '"') { ++p_; return out.size() <= lim_.maxStringBytes || Bad("string too long"); }
            if (c < 0x20) return Bad("control character in string");
            if (c == '\\') {
                if (++p_ >= s_.size()) return Bad("unterminated escape");
                const char e = s_[p_++];
                switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xDC00 && cp <= 0xDFFF) return Bad("lone low surrogate");
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        std::uint32_t lo;
                        if (p_ + 2 > s_.size() || s_[p_] != '\\' || s_[p_ + 1] != 'u') return Bad("lone high surrogate");
                        p_ += 2;
                        if (!Hex4(lo)) return false;
                        if (lo < 0xDC00 || lo > 0xDFFF) return Bad("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    if (!AppendUtf8(out, cp)) return Bad("bad code point");
                    break;
                }
                default: return Bad("bad escape");
                }
                continue;
            }
            const std::size_t len = Utf8Length(s_, p_);
            if (!len) return Bad("invalid UTF-8");
            out.append(s_.data() + p_, len);
            p_ += len;
        }
    }
    bool ParseNumber(Value& v) {
        const std::size_t start = p_;
        bool simple = true;
        if (p_ < s_.size() && s_[p_] == '-') ++p_;
        if (p_ >= s_.size()) return Bad("bad number");
        if (s_[p_] == '0') ++p_;
        else if (s_[p_] >= '1' && s_[p_] <= '9') { while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_; }
        else return Bad("unexpected character");
        if (p_ < s_.size() && s_[p_] == '.') {
            simple = false;
            ++p_;
            if (p_ >= s_.size() || s_[p_] < '0' || s_[p_] > '9') return Bad("bad fraction");
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
        }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            simple = false;
            ++p_;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
            if (p_ >= s_.size() || s_[p_] < '0' || s_[p_] > '9') return Bad("bad exponent");
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
        }
        if (p_ - start > 64) return Bad("number too long");
        const std::string text(s_.substr(start, p_ - start));
        v = Value();
        v.type = Type::Number;
        v.number = std::strtod(text.c_str(), nullptr);
        if (!std::isfinite(v.number)) return Bad("number out of range");
        if (simple && text.size() <= 19) {  // fits int64 when <= 18 digits (+sign); check exactly
            errno = 0;
            char* end = nullptr;
            const long long i = std::strtoll(text.c_str(), &end, 10);
            if (errno == 0 && end && *end == 0) { v.integral = true; v.integer = i; }
        }
        return true;
    }
};

inline void SerializeString(std::string& out, std::string_view s) {
    static const char* hex = "0123456789abcdef";
    out += '"';
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c) {
        case '"': out += "\\\""; continue;
        case '\\': out += "\\\\"; continue;
        case '\n': out += "\\n"; continue;
        case '\r': out += "\\r"; continue;
        case '\t': out += "\\t"; continue;
        case '\b': out += "\\b"; continue;
        case '\f': out += "\\f"; continue;
        default: break;
        }
        if (c < 0x20 || c == 0x7F) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
            continue;
        }
        if (c < 0x80) { out += char(c); continue; }
        std::uint32_t cp = 0;
        const std::size_t len = Utf8Length(s, i, &cp);
        if (!len) { out += "\\ufffd"; continue; }  // never emit invalid UTF-8
        if (cp == 0x2028) out += "\\u2028";
        else if (cp == 0x2029) out += "\\u2029";
        else out.append(s.data() + i, len);
        i += len - 1;
    }
    out += '"';
}

inline void SerializeNumber(std::string& out, const Value& v) {
    if (v.integral) { out += std::to_string(v.integer); return; }
    if (!std::isfinite(v.number)) { out += '0'; return; }
    char buffer[40];
    for (int precision = 15; precision <= 17; ++precision) {  // shortest of 15..17 digits that round-trips
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, v.number);
        if (std::strtod(buffer, nullptr) == v.number) break;
    }
    out += buffer;
}

inline void SerializeTo(std::string& out, const Value& v) {
    switch (v.type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += v.boolean ? "true" : "false"; break;
    case Type::Number: SerializeNumber(out, v); break;
    case Type::String: SerializeString(out, v.string); break;
    case Type::Array:
        out += '[';
        for (std::size_t i = 0; i < v.items.size(); ++i) {
            if (i) out += ',';
            SerializeTo(out, v.items[i]);
        }
        out += ']';
        break;
    case Type::Object:
        out += '{';
        for (std::size_t i = 0; i < v.members.size(); ++i) {
            if (i) out += ',';
            SerializeString(out, v.members[i].first);
            out += ':';
            SerializeTo(out, v.members[i].second);
        }
        out += '}';
        break;
    }
}

}  // namespace detail

inline bool Parse(std::string_view text, Value& out, std::string* error = nullptr, const Limits& limits = Limits()) {
    out = Value();
    detail::Parser parser(text, limits);
    if (!parser.Run(out, error)) {
        out = Value();
        return false;
    }
    return true;
}

inline std::string Serialize(const Value& v) {
    std::string out;
    detail::SerializeTo(out, v);
    return out;
}

// True when `s` is valid UTF-8 (same rules as the parser).
inline bool ValidUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const std::size_t len = detail::Utf8Length(s, i);
        if (!len) return false;
        i += len;
    }
    return true;
}

}  // namespace StrictJson
