#pragma once
#include <string>

namespace pt {

struct HttpResult {
    int code = 0;                 // CURLcode
    long status = 0;              // HTTP code
    std::string body;
};

HttpResult http(const std::string& url, const std::string& body,
                const std::string& bearer = {}, const char* accept = nullptr);

inline auto get(const std::string& u, const std::string& b = {}) { return http(u, {}, b); }
inline auto post(const std::string& u, const std::string& b, const std::string& br = {}) { return http(u, b, br); }
inline auto post_plain(const std::string& u, const std::string& b, const std::string& br = {}) { return http(u, b, br, "text/plain"); }
inline auto get_plain(const std::string& u, const std::string& br = "") { return http(u, {}, br, "text/plain"); }

// Probe 127.0.0.1 ports around `base` for a live serve --ttys relay.
std::string discover_relay(int base = 29325, int range = 10);

}  // namespace pt
