#include "http.hpp"
#include "json.hpp"
#include "render.hpp"
#include "store.hpp"
#include "discover.hpp"
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

// const-safe map lookup (operator[] is non-const). Declared early; defined
// just below host_from.
const std::string& get(const Args& a, const std::string& k);

std::string host_from(const Args& a) {
    auto it = a.opt.find("host");
    if (it != a.opt.end()) return it->second;
    if (const char* e = std::getenv("PROGTERM_HOST")) return e;
    pt::store::Account acc;
    if (pt::store::load_account("", acc) && !acc.host.empty()) return acc.host;
    if (!a.opt.count("no-scan")) {
        int base = 29325, range = 10;
        if (a.opt.count("scan-base"))  base  = std::stoi(get(a, "scan-base"));
        if (a.opt.count("scan-range")) range = std::stoi(get(a, "scan-range"));
        const std::string found = pt::discover_ttys_host(base, range);
        if (!found.empty()) return found;
    }
    return "http://127.0.0.1:29325";
}

// const-safe map lookup (operator[] is non-const).
const std::string& get(const Args& a, const std::string& k) {
    static const std::string empty;
    auto it = a.opt.find(k);
    return it == a.opt.end() ? empty : it->second;
}

// Resolve the active account: explicit --account wins, otherwise the current
// one. Command-line --host / --proxy override the cached values.
pt::store::Account resolve_account(const Args& a) {
    pt::store::Account acc;
    pt::store::load_account(get(a, "account"), acc);
    if (a.opt.count("host"))   acc.host = get(a, "host");
    if (a.opt.count("proxy"))  acc.proxy = get(a, "proxy");
    return acc;
}

void print_json_field(const pt::HttpResult& r, const std::string& field) {
    std::string v;
    if (pt::json::get_string(r.body, field, v)) std::cout << v << "\n";
}

int cmd_register(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal register [--name <n>] "
                     "--homeserver <url> --username <u> --password <p> "
                     "[--reg-token <t>] [--proxy <spec>]\n"
                     "  --name <n>    label this account (default: \"default\")\n"
                     "  --proxy <spec>  socks5://[u:p@]h:p | http://h:p | off "
                     "(per-account proxy; overrides server default)\n";
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
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty()) {
        pt::store::Account acc;
        acc.name = get(a, "name");
        if (acc.name.empty()) acc.name = "default";
        acc.host = host;
        acc.session = sid;
        acc.proxy = get(a, "proxy");
        pt::store::save_account(acc);
    }
    return 0;
}

int cmd_session(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal session [--name <n>] "
                     "--homeserver <url> --user <@id> --token <t> --device <d> "
                     "[--proxy <spec>]\n"
                     "  --name <n>    label this account (default: \"default\")\n"
                     "  --proxy <spec>  per-account proxy (socks5/http/off)\n";
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
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty()) {
        pt::store::Account acc;
        acc.name = get(a, "name");
        if (acc.name.empty()) acc.name = "default";
        acc.host = host;
        acc.session = sid;
        acc.proxy = get(a, "proxy");
        pt::store::save_account(acc);
    }
    return 0;
}

int cmd_input(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal input [--account <name>] "
                     "--text <line>\n";
        return 0;
    }
    const pt::store::Account acc = resolve_account(a);
    if (acc.session.empty()) { std::cerr << "error: no session (run register/session or use <name>)\n"; return 1; }
    const std::string body = "{"
        "\"session\":" + pt::json::str(acc.session) +
        ",\"input\":"   + pt::json::str(a.opt["text"]) + "}";
    pt::http_post_json(acc.host + "/api/ttys/input", body);
    return 0;
}

int cmd_sync(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal sync [--account <name>]\n";
        return 0;
    }
    const pt::store::Account acc = resolve_account(a);
    if (acc.session.empty()) { std::cerr << "error: no session (run register/session or use <name>)\n"; return 1; }
    const std::string body = "{\"session\":" + pt::json::str(acc.session) + "}";
    pt::HttpResult r = pt::http_post_json(acc.host + "/api/ttys/sync", body);
    std::cout << r.body << "\n";
    return 0;
}

int cmd_render(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { pt::usage_render(); return 0; }
    const pt::store::Account acc = resolve_account(a);
    if (acc.session.empty()) { std::cerr << "error: no session (run register/session or use <name>)\n"; return 1; }
    const bool static_only = a.opt.count("static") > 0;
    const int cols = a.opt.count("cols") ? std::stoi(get(a, "cols")) : 0;
    const int rows = a.opt.count("rows") ? std::stoi(get(a, "rows")) : 0;

#ifdef PROGTERM_TUI
    // Interactive mode when explicitly requested AND a TUI build.
    if (a.opt.count("tui")) {
        return pt::run_tui(acc.host, acc.session, get(a, "room"));
    }
#endif

    const std::string frame = pt::request_frame(acc.host, acc.session,
                                                get(a, "room"), static_only, cols, rows);
    if (frame.rfind("error:", 0) == 0) { std::cerr << frame << "\n"; return 1; }
    std::cout << frame;
    return 0;
}

int cmd_use(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal use <name>\n"
                     "  Make <name> the active account for subsequent commands.\n";
        return 0;
    }
    std::string name = get(a, "account");
    if (name.empty() && !a.pos.empty()) name = a.pos.front();
    if (name.empty()) { std::cerr << "error: usage: use <name>\n"; return 1; }
    pt::store::Account acc;
    if (!pt::store::load_account(name, acc)) {
        std::cerr << "error: unknown account '" << name << "'\n";
        return 1;
    }
    pt::store::set_current(name);
    std::cout << "active account: " << name << "\n";
    return 0;
}

int cmd_accounts(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal accounts\n"
                     "  List configured accounts and mark the active one.\n";
        return 0;
    }
    const std::string cur = pt::store::current_name();
    for (const auto& acc : pt::store::list_accounts()) {
        std::cout << (acc.name == cur ? "* " : "  ") << acc.name
                  << "  host=" << (acc.host.empty() ? "-" : acc.host)
                  << "  proxy=" << (acc.proxy.empty() ? "(server default)" : acc.proxy)
                  << "  session=" << (acc.session.empty() ? "(none)" : acc.session)
                  << "\n";
    }
    return 0;
}

int cmd_logout(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal logout [--account <name>] [--all]\n"
                     "  Forget an account (or all). Removes the local cache only.\n";
        return 0;
    }
    if (a.opt.count("all")) {
        pt::store::clear_all();
        std::cout << "all accounts cleared\n";
        return 0;
    }
    const std::string name = get(a, "account");
    if (pt::store::remove_account(name))
        std::cout << "account removed\n";
    else
        std::cout << "nothing to remove\n";
    return 0;
}

void usage() {
    std::cerr <<
        "progressive-terminal — lightweight curl-wrapper for progressive-chat (cli)\n\n"
        "Usage: progressive-terminal <command> [--host <url>] [options]\n\n"
        "Commands:\n"
        "  render     request the ASCII UI (detect terminal size, send, print)\n"
        "  register   POST /api/ttys/register -> saves the account\n"
        "  session    POST /api/ttys/session  -> saves the account\n"
        "  input      POST /api/ttys/input    (send one line to a session)\n"
        "  sync       POST /api/ttys/sync      (poll sync state)\n"
        "  use <name> switch the active account\n"
        "  accounts   list configured accounts\n"
        "  logout     forget an account (or --all)\n\n"
        "Global / per-command:\n"
        "  --host <url>     server endpoint (or $PROGTERM_HOST / cached host)\n"
        "  --no-scan        don't auto-scan nearby ports for the relay\n"
        "  --scan-base <p>  base port for scan (default 29325)\n"
        "  --scan-range <n> scan base±n ports (default 10)\n"
        "  --account <name> pick an account (render/input/sync); default=active\n"
        "  --name <name>    label an account on register/session (default)\n"
        "  --proxy <spec>   per-account proxy: socks5://[u:p@]h:p | http://h:p | off\n"
        "  -h, --help       this help\n\n"
        "Auto-connect: when no --host / $PROGTERM_HOST / cached host is set,\n"
        "  the client scans 127.0.0.1 ports around --scan-base (±--scan-range)\n"
        "  and connects to the first progressive-cli serve --ttys relay found.\n"
        "Multi-account: each register/session stores host+session+proxy under\n"
        "  $HOME/.config/progressive-terminal/accounts/<name>; 'use' selects the\n"
        "  active one. This is the ONLY local state (no message DB). 'logout'\n"
        "  removes it; the server still holds all real session state in RAM.\n\n"
        "render options:\n"
        "  --account <name> optional (uses the active account if omitted)\n"
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
    if (cmd == "use")      return cmd_use(a);
    if (cmd == "accounts") return cmd_accounts(a);
    if (cmd == "logout")   return cmd_logout(a);

    std::cerr << "unknown command: " << cmd << "\n";
    usage();
    return 1;
}
