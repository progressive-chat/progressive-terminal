#include "discover.hpp"
#include "http.hpp"
#include "json.hpp"
#include <string>

namespace pt {

std::string discover_ttys_host(int base, int range) {
    for (int d = 0; d <= range; ++d) {
        for (int sign : {1, -1}) {
            if (d == 0 && sign < 0) continue;  // base tried once
            const int port = base + (d == 0 ? 0 : sign * d);
            if (port <= 0 || port > 65535) continue;
            const std::string url = "http://127.0.0.1:" + std::to_string(port);
            // A real relay answers /api/ttys/sync (even for a bogus session)
            // with a JSON body carrying our "synced"/"error" fields. The status
            // may be 200 (valid) or 404 (unknown session) — the body markers
            // are what identify our server.
            pt::HttpResult r =
                http_post_json(url + "/api/ttys/sync",
                               "{\"session\":\"__probe__\"}");
            if (r.http_status >= 200 && r.http_status < 500) {
                std::string e, s;
                if (pt::json::get_string(r.body, "error", e) ||
                    pt::json::get_string(r.body, "synced", s))
                    return url;
            }
        }
    }
    return "";
}

}  // namespace pt
