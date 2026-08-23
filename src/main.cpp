#include "http.hpp"
#include "json.hpp"
#include "render.hpp"
#include "store.hpp"
#include "tui.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

struct Args {
    std::map<std::string, std::string> opt;
    std::vector<std::string> pos;
};

Args parse(int argc, char** argv) {
    Args a;
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (!s.empty() && s[0] == '-') {
            const size_t dash = (s.rfind("--", 0) == 0) ? 2 : 1;
            std::string key = s.substr(dash);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0 &&
                argv[i + 1][0] != '-') {
                a.opt[key] = argv[++i];
            } else {
                a.opt[key] = "1";
            }
        } else {
            a.pos.push_back(s);
        }
    }
    return a;
}

std::string host_from(const Args& a) {
    auto it = a.opt.find("host");
    if (it != a.opt.end()) return it->second;
    if (const char* e = std::getenv("PROGTERM_HOST")) return e;
    std::string h, s;
    if (pt::store::load_session(h, s) && !h.empty()) return h;
    return "http://127.0.0.1:29325";
}

// const-safe map lookup (operator[] is non-const).
const std::string& get(const Args& a, const std::string& k) {
    static const std::string empty;
    auto it = a.opt.find(k);
    return it == a.opt.end() ? empty : it->second;
}

// Resolve the session id: explicit --session wins, otherwise fall back to the
// cached one saved by a previous register/session (see pt::store).
std::string resolve_session(const Args& a) {
    std::string sid = get(a, "session");
    if (!sid.empty()) return sid;
    std::string h, s;
    if (pt::store::load_session(h, s)) return s;
    return "";
}

void print_json_field(const pt::HttpResult& r, const std::string& field) {
    std::string v;
    if (pt::json::get_string(r.body, field, v)) std::cout << v << "\n";
}

int cmd_register(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal register --homeserver <url> "
                     "--username <u> --password <p> [--reg-token <t>] [--proxy <spec>]\n"
                     "  --proxy <spec>  socks5://[u:p@]h:p | http://h:p | off "
                     "(overrides server default)\n";
        return 0;
    }
    const std::string host = host_from(a);
    const std::string body = "{"
        "\"homeserver\":" + pt::json::str(a.opt["homeserver"]) +
        ",\"username\":"   + pt::json::str(a.opt["username"]) +
        ",\"password\":"   + pt::json::str(a.opt["password"]) +
        ",\"reg_token\":"  + pt::json::str(a.opt["reg-token"]) +
        ",\"proxy\":"      + pt::json::str(a.opt["proxy"]) + "}";
    pt::HttpResult r = pt::http_post_json(host + "/api/ttys/register", body);
    if (!r.ok()) {
        std::string err;
        if (pt::json::get_string(r.body, "error", err)) std::cerr << "error: " << err << "\n";
        else std::cerr << "error: HTTP " << r.http_status << "\n";
        return 1;
    }
    for (const char* f : {"session", "user_id", "access_token", "device_id"})
        print_json_field(r, f);
    std::string sid;
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty())
        pt::store::save_session(host, sid);
    return 0;
}

int cmd_session(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal session --homeserver <url> "
                     "--user <@id> --token <t> --device <d> [--proxy <spec>]\n";
        return 0;
    }
    const std::string host = host_from(a);
    const std::string body = "{"
        "\"account\":{"
            "\"homeserver\":" + pt::json::str(a.opt["homeserver"]) +
            ",\"user_id\":"    + pt::json::str(a.opt["user"]) +
            ",\"access_token\":" + pt::json::str(a.opt["token"]) +
            ",\"device_id\":"  + pt::json::str(a.opt["device"]) +
            ",\"proxy\":"      + pt::json::str(a.opt["proxy"]) +
        "}}";
    pt::HttpResult r = pt::http_post_json(host + "/api/ttys/session", body);
    if (!r.ok()) {
        std::string err;
        if (pt::json::get_string(r.body, "error", err)) std::cerr << "error: " << err << "\n";
        else std::cerr << "error: HTTP " << r.http_status << "\n";
        return 1;
    }
    for (const char* f : {"session", "key"}) print_json_field(r, f);
    std::string sid;
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty())
        pt::store::save_session(host, sid);
    return 0;
}

int cmd_input(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal input --session <id> --text <line>\n";
        return 0;
    }
    const std::string host = host_from(a);
    const std::string sid = resolve_session(a);
    const std::string body = "{"
        "\"session\":" + pt::json::str(sid) +
        ",\"input\":"   + pt::json::str(a.opt["text"]) + "}";
    pt::http_post_json(host + "/api/ttys/input", body);
    return 0;
}

int cmd_sync(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal sync --session <id>\n";
        return 0;
    }
    const std::string host = host_from(a);
    const std::string sid = resolve_session(a);
    const std::string body = "{\"session\":" + pt::json::str(sid) + "}";
    pt::HttpResult r = pt::http_post_json(host + "/api/ttys/sync", body);
    std::cout << r.body << "\n";
    return 0;
}

int cmd_render(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { pt::usage_render(); return 0; }
    const std::string host = host_from(a);
    const std::string sid = resolve_session(a);
    const bool static_only = a.opt.count("static") > 0;
    const int cols = a.opt.count("cols") ? std::stoi(get(a, "cols")) : 0;
    const int rows = a.opt.count("rows") ? std::stoi(get(a, "rows")) : 0;

#ifdef PROGTERM_TUI
    // Interactive mode when explicitly requested AND a TUI build.
    if (a.opt.count("tui")) {
        if (sid.empty()) { std::cerr << "error: --session required (or run register/session first)\n"; return 1; }
        return pt::run_tui(host, sid, get(a, "room"));
    }
#endif

    const std::string frame = pt::request_frame(host, sid,
                                                get(a, "room"), static_only, cols, rows);
    if (frame.rfind("error:", 0) == 0) { std::cerr << frame << "\n"; return 1; }
    std::cout << frame;
    return 0;
}

int cmd_logout(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal logout\n"
                     "  Forget the cached session/host (removes the local cache file).\n";
        return 0;
    }
    if (pt::store::clear_session())
        std::cout << "cached session cleared\n";
    else
        std::cout << "nothing cached\n";
    return 0;
}

void usage() {
    std::cerr <<
        "progressive-terminal — lightweight curl-wrapper for progressive-chat (cli)\n\n"
        "Usage: progressive-terminal <command> [--host <url>] [options]\n\n"
        "Commands:\n"
        "  render     request the ASCII UI (detect terminal size, send, print)\n"
        "  register   POST /api/ttys/register -> prints session + credentials\n"
        "  session    POST /api/ttys/session  -> prints session id\n"
        "  input      POST /api/ttys/input    (send one line to a session)\n"
        "  sync       POST /api/ttys/sync      (poll sync state)\n"
        "  logout     forget the cached session/host\n\n"
        "Global:\n"
        "  --host <url>   server endpoint (or env PROGTERM_HOST,\n"
        "                 or the cached host from a previous register/session)\n"
        "  -h, --help     this help\n\n"
        "Note: register/session cache the (host, session) locally in\n"
        "  $HOME/.config/progressive-terminal/session, so later commands\n"
        "  can omit --session / --host. Run 'logout' to forget it.\n\n"
        "render options:\n"
        "  --session <id>  optional (uses the cached session if omitted)\n"
        "  --room <id>     optional room to focus\n"
        "  --static        single non-interactive snapshot (default)\n"
#ifdef PROGTERM_TUI
        "  --tui           interactive mode (compiled in)\n"
#endif
        "  --cols <n> --rows <n>  force size instead of detecting\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "help" ||
        (argc > 1 && std::string(argv[1]) == "-h")) {
        usage();
        return argc < 2 ? 1 : 0;
    }

    const std::string cmd = argv[1];
    Args a = parse(argc, argv);

    if (cmd == "render")   return cmd_render(a);
    if (cmd == "register") return cmd_register(a);
    if (cmd == "session")  return cmd_session(a);
    if (cmd == "input")    return cmd_input(a);
    if (cmd == "sync")     return cmd_sync(a);
    if (cmd == "logout")   return cmd_logout(a);

    std::cerr << "unknown command: " << cmd << "\n";
    usage();
    return 1;
}
