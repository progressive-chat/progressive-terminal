#include "json.hpp"
#include <cctype>

namespace pt { namespace json {

namespace {

// Escape a raw string for inclusion inside a JSON value (no quotes added).
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
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x",
                                  static_cast<unsigned char>(c));
                    o += buf;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

}  // namespace

std::string str(const std::string& s) {
    return "\"" + escape(s) + "\"";
}

namespace {

std::string unescape(std::string_view s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            switch (n) {
                case 'n': o += '\n'; i++; break;
                case 't': o += '\t'; i++; break;
                case 'r': o += '\r'; i++; break;
                case '"': o += '"';  i++; break;
                case '\\': o += '\\'; i++; break;
                case '/': o += '/';  i++; break;
                case 'u': {
                    if (i + 5 < s.size()) {
                        unsigned v = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i + 2 + k];
                            v <<= 4;
                            if (h >= '0' && h <= '9') v |= (h - '0');
                            else if (h >= 'a' && h <= 'f') v |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') v |= (h - 'A' + 10);
                        }
                        if (v < 0x80) o += static_cast<char>(v);  // ASCII (e.g. ESC=0x1b)
                        else o += '?';
                        i += 5;  // skip \uXXXX
                    } else {
                        o += 'u'; i++;
                    }
                    break;
                }
                default:  o += n;    i++; break;
            }
        } else {
            o += s[i];
        }
    }
    return o;
}

}  // namespace

static std::string_view ltrim(std::string_view v) {
    while (!v.empty() && std::isspace(static_cast<unsigned char>(v[0]))) v.remove_prefix(1);
    return v;
}

bool get_string(std::string_view body, std::string_view key, std::string& out) {
    std::string needle = "\"";
    needle.append(key.begin(), key.end());
    needle += "\"";

    size_t pos = body.find(needle);
    if (pos == std::string_view::npos) return false;
    pos += needle.size();
    // skip ws and colon
    while (pos < body.size() && (body[pos] == ':' || std::isspace(static_cast<unsigned char>(body[pos])))) pos++;
    if (pos >= body.size() || body[pos] != '"') return false;
    pos++;  // opening quote
    std::string raw;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            raw += body[pos];
            raw += body[pos + 1];
            pos += 2;
        } else {
            raw += body[pos];
            pos++;
        }
    }
    out = unescape(raw);
    return true;
}

}}  // namespace pt::json
