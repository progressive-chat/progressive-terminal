#include "render.hpp"
#include "http.hpp"
#include "json.hpp"
#include <iostream>
#include <cstdlib>

#ifdef __unix__
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace pt {

namespace {

struct TermSize { int cols = 80; int rows = 24; };

// Detect the current terminal size. Uses TIOCGWINSZ when available, then
// falls back to the COLUMNS/LINES environment variables, then 80x24.
TermSize detect_terminal_size() {
    TermSize s;
#ifdef TIOCGWINSZ
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        s.cols = static_cast<int>(w.ws_col);
        s.rows = static_cast<int>(w.ws_row);
        return s;
    }
#endif
    if (const char* c = std::getenv("COLUMNS"))
        if (const int v = std::atoi(c)) s.cols = v;
    if (const char* r = std::getenv("LINES"))
        if (const int v = std::atoi(r)) s.rows = v;
    return s;
}

}  // namespace

std::string request_frame(const std::string& host,
                          const std::string& session,
                          const std::string& room,
                          bool static_only,
                          int cols, int rows,
                          const std::string& bearer) {
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
    HttpResult r = http_post_json(url, body, bearer);
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
