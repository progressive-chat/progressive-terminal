#include "discover.hpp"
#include "http.hpp"
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
            // with a JSON body carrying our "synced" or "error" field; the
            // token-protected variant says "unauthorized". Substring check —
            // this branch deliberately has no JSON parser.
            const HttpResult r =
                http_post_json(url + "/api/ttys/sync",
                               "{\"session\":\"__probe__\"}");
            if (r.http_status >= 200 && r.http_status < 500 &&
                (r.body.find("\"synced\"") != std::string::npos ||
                 r.body.find("\"error\"") != std::string::npos))
                return url;
        }
    }
    return "";
}

}  // namespace pt
