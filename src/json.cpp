#include "json.hpp"
#include <cctype>
#include <cstdio>

namespace pt { namespace json {

namespace {

std::string escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    o += b;
                } else o += c;
        }
    }
    return o;
}

std::string unescape(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { o += s[i]; continue; }
        char n = s[++i];
        switch (n) {
            case 'n': o += '\n'; break;
            case 't': o += '\t'; break;
            case 'r': o += '\r'; break;
            case 'u':
                if (i + 4 < s.size()) {
                    unsigned v = 0;
                    for (int k = 1; k <= 4; ++k) {
                        char h = s[i + k];
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= h - '0';
                        else if (h >= 'a' && h <= 'f') v |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') v |= h - 'A' + 10;
                    }
                    i += 4;
                    o += v < 0x80 ? static_cast<char>(v) : '?';  // ASCII only
                } else o += 'u';
                break;
            default: o += n;  // covers " \\ / and anything else
        }
    }
    return o;
}

// Value of "key":"…" with escapes preserved, or "" when absent/not a string.
std::string raw_string_value(std::string_view body, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string_view::npos) return "\x01";  // "absent" sentinel
    pos += needle.size();
    while (pos < body.size() &&
           (body[pos] == ':' || std::isspace(static_cast<unsigned char>(body[pos]))))
        ++pos;
    if (pos >= body.size() || body[pos] != '"') return "\x01";
    ++pos;
    std::string raw;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) raw += body[pos++];
        raw += body[pos++];
    }
    if (pos > body.size()) return "\x01";
    return raw;
}

}  // namespace

std::string str(const std::string& s) { return "\"" + escape(s) + "\""; }

bool get_string(std::string_view body, std::string_view key, std::string& out) {
    const std::string raw = raw_string_value(body, key);
    if (raw == "\x01") return false;
    out = unescape(raw);
    return true;
}

}}  // namespace pt::json
