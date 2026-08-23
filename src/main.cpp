// progressive-terminal (dumb) — a zero-option pipe to `serve --ttys`.
//
//   progterm <path> [json-body]
//     One request to the relay. POST when a body is given, GET otherwise.
//     The path is used verbatim (leading '/' optional); the body is sent
//     exactly as typed — the client adds nothing and parses nothing.
//
//   Environment:
//     PROGTERM_HOST    relay address; else scan 127.0.0.1 near :29325,
//                      else http://127.0.0.1:29325
//     PROGTERM_TOKEN   sent as "Authorization: Bearer" when set
//
//   Exit codes: 0 = 2xx, 1 = HTTP error (body printed anyway), 2 = no route
//   to relay. The response body always goes to stdout untouched.

#include "http.hpp"
#include "discover.hpp"

#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <vector>

#ifdef __unix__
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace {

std::string host_from() {
    if (const char* e = std::getenv("PROGTERM_HOST")) return e;
    const std::string found = pt::discover_ttys_host(29325, 10);
    return found.empty() ? "http://127.0.0.1:29325" : found;
}

std::string bearer_from() {
    const char* e = std::getenv("PROGTERM_TOKEN");
    return e ? e : "";
}

// Optional store-and-forward: PROGTERM_OUTBOX=<file>. POSTs that cannot
// reach the relay are appended as "<path>\t<body>" lines and flushed oldest
// first on the next successful contact. Without the variable the client
// stays zero-storage.
std::string outbox_path() {
    const char* e = std::getenv("PROGTERM_OUTBOX");
    return e ? e : "";
}

void outbox_flush(const std::string& host, const std::string& bearer) {
    const std::string p = outbox_path();
    if (p.empty()) return;
    std::ifstream f(p);
    if (!f) return;
    std::vector<std::string> pending;
    for (std::string l; std::getline(f, l);)
        if (!l.empty()) pending.push_back(l);
    f.close();
    if (pending.empty()) return;
    std::ofstream(p, std::ios::trunc);  // clear first; failures re-append
    for (const auto& l : pending) {
        const size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        const pt::HttpResult r = pt::http_post_json(
            host + "/" + l.substr(0, tab), l.substr(tab + 1), bearer);
        if (r.http_status == 0)
            std::ofstream(p, std::ios::app) << l << '\n';  // still offline
    }
}

bool outbox_record(const std::string& path, const std::string& body) {
    const std::string p = outbox_path();
    if (p.empty()) return false;
    std::ofstream(p, std::ios::app) << path << '\t' << body << '\n';
    std::cerr << "progterm: offline — query spooled\n";
    return true;
}

// Terminal size: TIOCGWINSZ when attached to a tty, then COLUMNS/LINES,
// then 80x24. The one piece of LOCAL data a dumb pipe still must supply.
void term_size(int& cols, int& rows) {
    cols = 80; rows = 24;
#ifdef TIOCGWINSZ
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        cols = static_cast<int>(w.ws_col);
        rows = static_cast<int>(w.ws_row);
        return;
    }
#endif
    if (const char* c = std::getenv("COLUMNS"))
        if (const int v = std::atoi(c)) cols = v;
    if (const char* r = std::getenv("LINES"))
        if (const int v = std::atoi(r)) rows = v;
}

// Escape a raw line as a JSON string value (no surrounding quotes). The
// only place the dumb client formats data — input lines can hold anything.
std::string jesc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else o += c;
    }
    return o;
}

// progterm term <session> [room] — the remote-terminal loop: every stdin
// line is delivered to the full client's REPL, then the refreshed screen
// comes back as plain text and is printed verbatim.
int term_loop(const std::string& host, const std::string& bearer, int argc,
              char** argv) {
    const std::string session = argv[2];
    const std::string room = argc >= 4 ? argv[3] : "";
    const std::string in_url = host + "/api/ttys/input";
    const std::string rd_url = host + "/api/ttys/render";
    auto frame_body = [&] {
        int c, r; term_size(c, r);   // re-detected every frame: resizes live
        return "{\"session\":\"" + jesc(session) + "\""
               ",\"term\":{\"cols\":" + std::to_string(c) +
               ",\"rows\":" + std::to_string(r) + "}" +
               (room.empty() ? "" : ",\"room\":\"" + jesc(room) + "\"") +
               ",\"view\":\"static\"}";
    };

    pt::http_post_plain(rd_url, frame_body(), bearer);  // first paint
    std::cout << "term> " << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == ":q" || line == "/quit") return 0;
        if (!line.empty())
            pt::http_post_json(in_url,
                "{\"session\":\"" + jesc(session) +
                "\",\"input\":\"" + jesc(line) + "\"}", bearer);
        const pt::HttpResult fr = pt::http_post_plain(rd_url, frame_body(), bearer);
        if (fr.http_status == 200)
            std::cout << "\x1b[2J\x1b[H" << fr.body << "term> " << std::flush;
        else
            std::cerr << "[" << fr.http_status << "] " << fr.body << "\nterm> "
                      << std::flush;
    }
    return 0;
}

// progterm render <session> [room] — one static ASCII frame, sized to the
// local terminal. A positional verb, not an option: no flags exist here.
std::string render_body(int argc, char** argv) {
    int c, r;
    term_size(c, r);
    std::string b = "{\"session\":\"" + std::string(argv[2]) + "\"" +
                    ",\"term\":{\"cols\":" + std::to_string(c) +
                    ",\"rows\":" + std::to_string(r) + "}";
    if (argc >= 4) b += ",\"room\":\"" + std::string(argv[3]) + "\"";
    return b + ",\"view\":\"static\"}";
}

// ---- queries caught BEFORE the wire -------------------------------------
// Special verbs: either purely local (help) or formatters around one call,
// so the user never writes JSON for the routine cases.

int cmd_last(const std::string& host, const std::string& bearer) {
    const pt::HttpResult r =
        pt::http_get_json(host + "/api/ttys/session/last", bearer);
    std::cout << r.body << "\n";
    return r.http_status == 200 ? 0 : 1;
}

int cmd_sync(const std::string& host, const std::string& bearer, int argc,
             char** argv) {
    if (argc < 3) { std::cerr << "usage: progterm sync <session>\n"; return 1; }
    const std::string body =
        "{\"session\":\"" + jesc(argv[2]) + "\"}";
    const pt::HttpResult r = pt::http_post_plain(
        host + "/api/ttys/sync", body, bearer);
    std::cout << r.body << "\n";
    return r.http_status == 200 ? 0 : 1;
}

int cmd_proxy(const std::string& host, const std::string& bearer, int argc,
              char** argv) {
    std::string body;
    if (argc >= 4 && argv[2] == std::string("on"))
        body = "{\"action\":\"on\",\"preset\":\"" + jesc(argv[3]) + "\"}";
    else if (argc >= 3 && argv[2] == std::string("off"))
        body = "{\"action\":\"off\"}";
    if (body.empty()) {  // no args -> status (GET)
        const pt::HttpResult r =
            pt::http_get_json(host + "/api/ttys/proxy", bearer);
        std::cout << r.body << "\n";
        return r.http_status == 200 ? 0 : 1;
    }
    const pt::HttpResult r =
        pt::http_post_json(host + "/api/ttys/proxy", body, bearer);
    std::cout << r.body << "\n";
    return r.http_status == 200 ? 0 : 1;
}

void usage() {
    std::cerr << "usage: progterm <path> [json-body]\n"
                 "env:   PROGTERM_HOST  PROGTERM_TOKEN\n"
                 "verbs caught locally: help | last | sync <session> |\n"
              << "  proxy [on <preset>|off] | render <session> [room] | term <session> [room]\n"
              << "example: progterm proxy on tor\n"
              << "         progterm render <session> [room]   # static frame, "
              "auto-sized\n"
              << "         progterm term <session> [room]     # remote-terminal loop\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string path = argv[1];
    if (!path.empty() && path[0] == '/') path.erase(0, 1);
    if (path == "help") { usage(); return 0; }

    const std::string host = host_from();
    const std::string bearer = bearer_from();
    outbox_flush(host, bearer);          // deliver anything spooled earlier

    if (path == "last")  return cmd_last(host, bearer);
    if (path == "sync")  return cmd_sync(host, bearer, argc, argv);
    if (path == "proxy") return cmd_proxy(host, bearer, argc, argv);

    std::string body;
    if (path == "render") {
        if (argc < 3) { usage(); return 1; }
        body = render_body(argc, argv);            // sized locally
        path = "api/ttys/render";
    } else if (path != "term" && argc >= 3) {
        body = argv[2];                            // verbatim pipe
    }

    if (path != "term") {
        const bool want_plain = (path == "api/ttys/render");
        const pt::HttpResult r = body.empty()
            ? pt::http_get_json(host + "/" + path, bearer)
            : want_plain ? pt::http_post_plain(host + "/" + path, body, bearer)
                         : pt::http_post_json(host + "/" + path, body, bearer);
        if (r.http_status == 0) {
            if (!body.empty() && outbox_record(path, body)) return 0;
            std::cerr << "progterm: cannot reach relay (curl "
                      << static_cast<long>(r.code) << ")\n";
            return 2;
        }
        std::cout << r.body << "\n";
        return (r.http_status >= 200 && r.http_status < 300) ? 0 : 1;
    }

    // ---- remote-terminal loop ----
    if (argc < 3) { usage(); return 1; }
    return term_loop(host, bearer, argc, argv);
}
