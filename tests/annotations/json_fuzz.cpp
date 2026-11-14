// StrictJson tests: fixed accept/reject vectors, bounds, a deterministic mutation fuzzer and a random-tree
// round trip. `json_fuzz.exe --check` reads one document per stdin line and prints "OK <canonical>" or "ERR"
// (used by run_tests.py for a differential test against Python's json module).
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/strict-json.h"

namespace SJ = StrictJson;

namespace {
int failures = 0, checks = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        ++checks;                                                       \
        if (!(cond)) {                                                  \
            ++failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

struct Rng {
    std::uint64_t s;
    std::uint64_t Next() {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    std::size_t Below(std::size_t n) { return n ? std::size_t(Next() % n) : 0; }
};

bool Accepts(const std::string& s, const SJ::Limits& l = SJ::Limits()) {
    SJ::Value v;
    return SJ::Parse(s, v, nullptr, l);
}

// Invariants for any accepted document: canonical output is one line, valid UTF-8, re-parses to itself.
void CheckCanonical(const SJ::Value& v) {
    const std::string a = SJ::Serialize(v);
    bool clean = SJ::ValidUtf8(a);
    for (unsigned char c : a)
        if (c < 0x20) clean = false;
    CHECK(clean);
    SJ::Value again;
    SJ::Limits big;
    big.maxDepth = 1000;
    big.maxItems = 1u << 22;
    big.maxBytes = 1u << 26;
    big.maxStringBytes = 1u << 26;
    CHECK(SJ::Parse(a, again, nullptr, big));
    CHECK(SJ::Serialize(again) == a);
}

void FixedVectors() {
    const char* good[] = {"null", "true", "false", "0", "-0", "1.5e-3", "-12.25E+2", "\"\"", "\"\\u00e9\\ud83d\\ude00\"",
                          "\"\xC3\xA9\xF0\x9F\x98\x80\"", "[]", "{}", " [1, {\"a\": [null]}] \r\n", "{\"a\":1,\"b\":2}",
                          "\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"", "123456789012345678", "1e308"};
    for (const char* g : good) {
        SJ::Value v;
        CHECK(SJ::Parse(g, v));
        CheckCanonical(v);
    }
    const char* bad[] = {"", " ", "nul", "True", "NaN", "Infinity", "-Infinity", "01", "1.", ".5", "-", "+1", "1e", "1e+",
                         "0x10", "1e400", "-1e400", "\"abc", "\"\\x\"", "\"\\u12\"", "\"\\ud800\"", "\"\\udc00\"",
                         "\"\\ud800\\u0041\"", "\"a\nb\"", "\"\t\"", "[1,]", "[,1]", "{\"a\":1,}", "{\"a\" 1}", "{a:1}",
                         "{'a':1}", "{\"a\":1,\"a\":2}", "[1] [2]", "1 2", "/*c*/1", "[1]//", "\xEF\xBB\xBF{}",
                         "\"\xC0\xAF\"", "\"\xED\xA0\x80\"", "\"\xF4\x90\x80\x80\"", "\"\xFF\"", "\"\x80\"", "\"\xE2\x82\"",
                         "{\"a\":}", "[\"a\" \"b\"]"};
    for (const char* b : bad) CHECK(!Accepts(b));
    CHECK(!Accepts(std::string("[1]\0", 4)));  // NUL cases need explicit lengths
    CHECK(!Accepts(std::string("\0", 1)));
    CHECK(!Accepts(std::string("\"a\0b\"", 5)));
    SJ::Limits bom;
    bom.allowBom = true;
    CHECK(Accepts("\xEF\xBB\xBF{}", bom));

    SJ::Value v;
    CHECK(SJ::Parse("{\"i\":42,\"f\":0.1,\"big\":12345678901234567890,\"neg\":-7}", v));
    CHECK(v.Get("i")->integral && v.Get("i")->integer == 42);
    CHECK(!v.Get("f")->integral && v.Get("f")->number == 0.1);
    CHECK(!v.Get("big")->integral);
    CHECK(v.Get("neg")->integral && v.Get("neg")->integer == -7);
    CHECK(SJ::Serialize(v) == "{\"i\":42,\"f\":0.1,\"big\":1.2345678901234567e+19,\"neg\":-7}");
    CHECK(SJ::Serialize(SJ::Value::Str("a\nb\x01\x7f\xE2\x80\xA8</script>\xFF")) == "\"a\\nb\\u0001\\u007f\\u2028</script>\\ufffd\"");
}

void Bounds() {
    SJ::Limits l;
    l.maxDepth = 32;
    std::string ok(32, '['), deep(33, '[');
    ok += std::string(32, ']');
    deep += std::string(33, ']');
    CHECK(Accepts(ok, l));
    CHECK(!Accepts(deep, l));
    std::string bomb(200000, '[');  // no stack overflow: depth is checked before recursing
    CHECK(!Accepts(bomb));
    std::string objBomb;
    for (int i = 0; i < 100000; ++i) objBomb += "{\"a\":";
    CHECK(!Accepts(objBomb));
    l = SJ::Limits();
    l.maxBytes = 10;
    CHECK(!Accepts("[1,2,3,4,5,6]", l));
    l = SJ::Limits();
    l.maxStringBytes = 4;
    CHECK(Accepts("\"abcd\"", l));
    CHECK(!Accepts("\"abcde\"", l));
    CHECK(!Accepts("{\"abcde\":1}", l));
    l = SJ::Limits();
    l.maxItems = 3;
    CHECK(Accepts("[1,2,3]", l));
    CHECK(!Accepts("[1,2,3,4]", l));
    CHECK(!Accepts("[[1],[2]]", l));
    std::string many = "[";
    for (int i = 0; i < 70000; ++i) many += i ? ",0" : "0";
    many += "]";
    CHECK(!Accepts(many));  // default maxItems 65536
    CHECK(!Accepts("1" + std::string(100, '0')));  // number lexeme too long
}

SJ::Value RandomValue(Rng& r, int depth) {
    switch (r.Below(depth > 4 ? 5 : 7)) {
    case 0: return SJ::Value();
    case 1: return SJ::Value::Bool(r.Below(2));
    case 2: return SJ::Value::Int(std::int64_t(r.Next() % 2000001) - 1000000);
    case 3: return SJ::Value::Num(double(std::int64_t(r.Next() % 2000001) - 1000000) / double(1 + r.Below(1000)));
    case 4: {
        std::string s;
        const std::size_t n = r.Below(12);
        static const char* pieces[] = {"a", "\"", "\\", "\n", "\x01", "\xC3\xA9", "\xF0\x9F\x98\x80", "\xE2\x80\xA8", "/", " ", "`", "}"};
        for (std::size_t i = 0; i < n; ++i) s += pieces[r.Below(12)];
        return SJ::Value::Str(s);
    }
    case 5: {
        SJ::Value a = SJ::Value::Arr();
        const std::size_t n = r.Below(5);
        for (std::size_t i = 0; i < n; ++i) a.Push(RandomValue(r, depth + 1));
        return a;
    }
    default: {
        SJ::Value o = SJ::Value::Obj();
        const std::size_t n = r.Below(5);
        for (std::size_t i = 0; i < n; ++i) o.Set("k" + std::to_string(r.Below(8)) + (r.Below(2) ? "\xC3\xA9" : ""), RandomValue(r, depth + 1));
        return o;
    }
    }
}

void RoundTrip() {
    Rng r{0x9E3779B97F4A7C15ull};
    for (int i = 0; i < 20000; ++i) {
        const SJ::Value v = RandomValue(r, 0);
        const std::string text = SJ::Serialize(v);
        SJ::Value back;
        CHECK(SJ::Parse(text, back));
        CHECK(SJ::Serialize(back) == text);
    }
}

void Mutations() {
    Rng r{0xD1B54A32D192ED03ull};
    std::vector<std::string> seeds = {
        "{\"schema\":\"browser-annotation/1\",\"id\":\"ann_abcdefghijklmnopqrstuvwxyz\",\"note\":\"\\u00e9 \xF0\x9F\x98\x80\","
        "\"target\":{\"rect\":{\"x\":1.5,\"y\":-2e3},\"path\":[\"html\",\"body\"],\"selectionText\":null},\"flag\":true}",
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"x\",\"arguments\":{\"a\":[1,2,{}]}}}",
        "[\"\\ud83d\\ude00\",\"\\\\\",0,-0.0,1e10,[],[[]],{\"\":\"\"}]"};
    static const char interesting[] = "{}[]\",:\\/0123456789eE+-.tfnu \n\t\x01\x7f\x80\xC0\xC3\xED\xEF\xF0\xF4\xFF";
    long accepted = 0;
    const int iterations = 300000;
    for (int i = 0; i < iterations; ++i) {
        std::string s = seeds[r.Below(seeds.size())];
        const std::size_t edits = 1 + r.Below(4);
        for (std::size_t k = 0; k < edits && !s.empty(); ++k) {
            const std::size_t at = r.Below(s.size());
            switch (r.Below(6)) {
            case 0: s[at] = char(s[at] ^ (1 << r.Below(8))); break;
            case 1: s.insert(s.begin() + long(at), interesting[r.Below(sizeof interesting - 1)]); break;
            case 2: s.erase(at, 1 + r.Below(4)); break;
            case 3: s.insert(at, s.substr(r.Below(s.size()), r.Below(16))); break;
            case 4: s.resize(at); break;
            default: s[at] = interesting[r.Below(sizeof interesting - 1)]; break;
            }
        }
        SJ::Value v;
        if (SJ::Parse(s, v)) {
            ++accepted;
            CheckCanonical(v);
            if (accepted % 64 == 0 && seeds.size() < 64) seeds.push_back(s);  // grow the corpus with valid shapes
        }
    }
    std::printf("json_fuzz: %d mutations, %ld accepted, corpus %zu\n", iterations, accepted, seeds.size());
    CHECK(accepted > 100);
}

int CheckMode() {
    // One document per line; lines are hex-encoded so that any byte (newline, NUL) can be tested.
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::string data;
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(in, buf, sizeof buf, &got, nullptr) && got) data.append(buf, got);
    std::string out;
    std::size_t start = 0;
    while (start < data.size()) {
        std::size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        std::string hex = data.substr(start, end - start);
        if (!hex.empty() && hex.back() == '\r') hex.pop_back();
        start = end + 1;
        std::string doc;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) doc += char(std::stoi(hex.substr(i, 2), nullptr, 16));
        SJ::Value v;
        if (SJ::Parse(doc, v)) out += "OK " + SJ::Serialize(v) + "\n";
        else out += "ERR\n";
    }
    DWORD put = 0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), out.data(), DWORD(out.size()), &put, nullptr);
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--check") return CheckMode();
    FixedVectors();
    Bounds();
    RoundTrip();
    Mutations();
    std::printf("json_fuzz: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
