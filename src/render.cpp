#include "render.hpp"
#include "http.hpp"
#include "json.hpp"
#include "term_size.hpp"
#include <iostream>

namespace pt {

std::string request_frame(const std::string& host,
                          const std::string& session,
                          const std::string& room,
                          bool static_only,
                          int cols, int rows) {
    if (session.empty())
        return "error: --session is required (register or `session` first)";

    TermSize sz;
    if (cols > 0 && rows > 0) {
        sz.cols = cols; sz.rows = rows;
    } else {
        sz = detect_terminal_size();
    }

    std::string body = "{";
    body += "\"session\":" + json::str(session);
    body += ",\"term\":{\"cols\":" + std::to_string(sz.cols) +
            ",\"rows\":" + std::to_string(sz.rows) + "}";
    if (!room.empty())
        body += ",\"room\":" + json::str(room);
    if (static_only)
        body += ",\"view\":\"static\"";
    body += "}";

    const std::string url = host + "/api/ttys/render";
    HttpResult r = http_post_json(url, body);
    if (!r.ok()) {
        std::string err;
        if (json::get_string(r.body, "error", err))
            return "error: " + err;
        return "error: HTTP " + std::to_string(r.http_status) +
               " (curl " + std::to_string(static_cast<long>(r.code)) + ")";
    }

    std::string frame;
    if (!json::get_string(r.body, "frame", frame))
        return "error: no 'frame' in response";
    return frame;
}

void usage_render() {
    std::cerr <<
        "Usage: progressive-terminal render [--account <name>] "
        "[options]\n"
        "  --account <name> account to render (defaults to the active one)\n"
        "  --host <url>     progressive-cli serve --ttys endpoint "
        "(or $PROGTERM_HOST)\n"
        "  --room <id>      optional room to focus\n"
        "  --static         request a single non-interactive ASCII snapshot\n"
        "  --cols <n>       force width (default: detect terminal)\n"
        "  --rows <n>       force height (default: detect terminal)\n";
}

}  // namespace pt
