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
#include <cstdlib>

namespace {

struct Args {
    std::map<std::string, std::string> opt;
    std::vector<std::string> pos;
};

// const-safe map lookup (operator[] is non-const).
const std::string& get(const Args& a, const std::string& k) {
    static const std::string empty;
    auto it = a.opt.find(k);
    return it == a.opt.end() ? empty : it->second;
}

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
    if (a.opt.count("host")) return get(a, "host");
    if (const char* e = std::getenv("PROGTERM_HOST")) return e;
    pt::store::Profile p;
    if (pt::store::load_profile("", p) && !p.host.empty()) return p.host;
    if (!a.opt.count("no-scan")) {
        int base = 29325, range = 10;
        if (a.opt.count("scan-base"))  base  = std::stoi(get(a, "scan-base"));
        if (a.opt.count("scan-range")) range = std::stoi(get(a, "scan-range"));
        const std::string found = pt::discover_ttys_host(base, range);
        if (!found.empty()) return found;
    }
    return "http://127.0.0.1:29325";
}

// Relay auth token: --relay-token wins, else $PROGTERM_TOKEN. Sent as an
// "Authorization: Bearer" header when non-empty (matches the relay's
// `serve --ttys --token` option).
std::string bearer_from(const Args& a) {
    if (a.opt.count("relay-token")) return get(a, "relay-token");
    if (const char* e = std::getenv("PROGTERM_TOKEN")) return e;
    return "";
}

// Resolve the active profile: explicit --profile wins, else the current one.
// --host / --proxy on the command line override the stored values.
pt::store::Profile resolve_profile(const Args& a) {
    pt::store::Profile p;
    pt::store::load_profile(get(a, "profile"), p);
    if (a.opt.count("host"))  p.host = get(a, "host");
    if (a.opt.count("proxy")) p.proxy = get(a, "proxy");
    return p;
}

void print_json_field(const pt::HttpResult& r, const std::string& field) {
    std::string v;
    if (pt::json::get_string(r.body, field, v)) std::cout << v << "\n";
}

void usage_render() {
    std::cerr <<
        "Usage: progressive-terminal render [--profile <name>] [options]\n"
        "  --profile <name> profile to render (defaults to the active one)\n"
        "  --host <url>      progressive-cli serve --ttys endpoint "
        "(or $PROGTERM_HOST)\n"
        "  --room <id>       optional room to focus\n"
        "  --static          request a single non-interactive ASCII snapshot\n"
        "  --cols <n>        force width (default: detect terminal)\n"
        "  --rows <n>        force height (default: detect terminal)\n";
}

void usage_register() {
    std::cerr << "Usage: progressive-terminal register [--profile <n>] "
                 "--homeserver <url> --username <u> --password <p> "
                 "[--reg-token <t>] [--proxy <spec>] [--relay-token <t>]\n"
                 "  --profile <n>  store the account in this profile (default: active)\n"
                 "  --proxy <spec> socks5://[u:p@]h:p | http://h:p | off "
                 "(per-profile proxy; overrides server default)\n";
}

void usage_session() {
    std::cerr << "Usage: progressive-terminal session [--profile <n>] "
                 "--homeserver <url> --user <@id> --token <t> "
                 "--device <d> [--proxy <spec>] [--relay-token <t>]\n"
                 "  --profile <n>  store the account in this profile\n"
                 "  --proxy <spec> per-profile proxy (socks5/http/off)\n";
}

void usage_proxy() {
    std::cerr << "Usage: progressive-terminal proxy <on|off|status> [args]\n"
                 "  proxy on  <preset>   enable a server-side proxy preset "
                 "(e.g. tor, i2p)\n"
                 "  proxy off            disable the server-side proxy\n"
                 "  proxy status         show local profile proxy + relay status\n"
                 "  Auth: --relay-token <t> or $PROGTERM_TOKEN when the relay "
                 "runs with --token\n";
}

void usage_profile() {
    std::cerr << "Usage: progressive-terminal profile <action> [name] [flags]\n"
                 "  profile create <name> [--proxy <spec>]   new profile (no account needed)\n"
                 "  profile set <name> --proxy <spec>        set proxy on a profile\n"
                 "  profile enable <name> | disable <name>  toggle (>=1 stays enabled)\n"
                 "  profile current <name>                   make <name> the active profile\n"
                 "  profile delete <name>                    remove (refuses last enabled)\n"
                 "  profile list                            show all profiles\n";
}

int cmd_register(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { usage_register(); return 0; }
    std::string pname = get(a, "profile");
    if (pname.empty()) { pt::store::ensure_default_profile(); pname = pt::store::current_name(); }
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) { p.name = pname; p.enabled = true; }
    const std::string host = host_from(a);
    const std::string body = "{"
        "\"homeserver\":" + pt::json::str(a.opt["homeserver"]) +
        ",\"username\":"   + pt::json::str(a.opt["username"]) +
        ",\"password\":"   + pt::json::str(a.opt["password"]) +
        ",\"reg_token\":"  + pt::json::str(a.opt["reg-token"]) +
        ",\"proxy\":"      + pt::json::str(get(a, "proxy")) + "}";
    pt::HttpResult r = pt::http_post_json(host + "/api/ttys/register", body,
                                          bearer_from(a));
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
        p.session = sid; p.host = host; p.proxy = get(a, "proxy");
        pt::store::save_profile(p);
        pt::store::set_current(p.name);
    }
    return 0;
}

int cmd_session(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { usage_session(); return 0; }
    std::string pname = get(a, "profile");
    if (pname.empty()) { pt::store::ensure_default_profile(); pname = pt::store::current_name(); }
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) { p.name = pname; p.enabled = true; }
    const std::string host = host_from(a);
    const std::string body = "{"
        "\"account\":{"
            "\"homeserver\":" + pt::json::str(a.opt["homeserver"]) +
            ",\"user_id\":"    + pt::json::str(a.opt["user"]) +
            ",\"access_token\":" + pt::json::str(a.opt["token"]) +
            ",\"device_id\":"  + pt::json::str(a.opt["device"]) +
            ",\"proxy\":"      + pt::json::str(get(a, "proxy")) +
        "}}";
    pt::HttpResult r = pt::http_post_json(host + "/api/ttys/session", body,
                                          bearer_from(a));
    if (!r.ok()) {
        std::string err;
        if (pt::json::get_string(r.body, "error", err)) std::cerr << "error: " << err << "\n";
        else std::cerr << "error: HTTP " << r.http_status << "\n";
        return 1;
    }
    for (const char* f : {"session", "key"}) print_json_field(r, f);
    std::string sid;
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty()) {
        p.session = sid; p.host = host; p.proxy = get(a, "proxy");
        pt::store::save_profile(p);
        pt::store::set_current(p.name);
    }
    return 0;
}

int cmd_input(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal input [--profile <name>] "
                     "--text <line>\n";
        return 0;
    }
    const pt::store::Profile p = resolve_profile(a);
    if (p.session.empty()) { std::cerr << "error: no session (register into a profile first)\n"; return 1; }
    const std::string body = "{"
        "\"session\":" + pt::json::str(p.session) +
        ",\"input\":"   + pt::json::str(a.opt["text"]) + "}";
    pt::http_post_json(p.host + "/api/ttys/input", body, bearer_from(a));
    return 0;
}

int cmd_sync(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal sync [--profile <name>]\n";
        return 0;
    }
    const pt::store::Profile p = resolve_profile(a);
    if (p.session.empty()) { std::cerr << "error: no session (register into a profile first)\n"; return 1; }
    const std::string body = "{\"session\":" + pt::json::str(p.session) + "}";
    pt::HttpResult r = pt::http_post_json(p.host + "/api/ttys/sync", body,
                                          bearer_from(a));
    std::cout << r.body << "\n";
    return 0;
}

int cmd_render(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { usage_render(); return 0; }
    const pt::store::Profile p = resolve_profile(a);
    if (p.session.empty()) { std::cerr << "error: no session (register into a profile first)\n"; return 1; }
    const bool static_only = a.opt.count("static") > 0;
    const int cols = a.opt.count("cols") ? std::stoi(get(a, "cols")) : 0;
    const int rows = a.opt.count("rows") ? std::stoi(get(a, "rows")) : 0;

#ifdef PROGTERM_TUI
    if (a.opt.count("tui")) {
        return pt::run_tui(p.host, p.session, get(a, "room"), bearer_from(a));
    }
#endif

    const std::string frame = pt::request_frame(p.host, p.session,
                                                get(a, "room"), static_only,
                                                cols, rows, bearer_from(a));
    if (frame.rfind("error:", 0) == 0) { std::cerr << frame << "\n"; return 1; }
    std::cout << frame;
    return 0;
}

int cmd_use(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal use <name>\n"
                     "  Make <name> the active profile (must be enabled).\n";
        return 0;
    }
    std::string name = get(a, "profile");
    if (name.empty() && !a.pos.empty()) name = a.pos.front();
    if (name.empty()) { std::cerr << "error: usage: use <name>\n"; return 1; }
    if (!pt::store::set_current(name)) {
        std::cerr << "error: unknown or disabled profile '" << name << "'\n";
        return 1;
    }
    std::cout << "active profile: " << name << "\n";
    return 0;
}

int cmd_accounts(Args& a) {
    // alias for `profile list`
    std::cout << "profiles:\n";
    const std::string cur = pt::store::current_name();
    for (const auto& p : pt::store::list_profiles()) {
        std::cout << (p.name == cur ? "* " : "  ") << p.name
                  << (p.enabled ? "  [enabled]" : "  [disabled]")
                  << "  proxy=" << (p.proxy.empty() ? "(server default)" : p.proxy)
                  << "  session=" << (p.session.empty() ? "(none)" : p.session)
                  << "\n";
    }
    return 0;
}

int cmd_profile(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { usage_profile(); return 0; }
    std::string sub = a.pos.empty() ? "" : a.pos[0];
    if (sub == "list" || sub.empty()) return cmd_accounts(a);

    if (sub == "create" || sub == "set") {
        std::string name = a.pos.size() > 1 ? a.pos[1] : get(a, "name");
        if (name.empty()) { std::cerr << "error: profile name required\n"; return 1; }
        pt::store::Profile p;
        if (!pt::store::load_profile(name, p)) { p.name = name; p.enabled = true; }
        if (sub == "set" && a.opt.count("proxy")) p.proxy = get(a, "proxy");
        if (sub == "create" && a.opt.count("proxy")) p.proxy = get(a, "proxy");
        if (sub == "create") p.enabled = true;
        pt::store::save_profile(p);
        std::cout << "profile " << name << " saved (proxy="
                  << (p.proxy.empty() ? "server default" : p.proxy) << ")\n";
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        std::string name = a.pos.size() > 1 ? a.pos[1] : get(a, "name");
        if (name.empty()) { std::cerr << "error: profile name required\n"; return 1; }
        const bool on = (sub == "enable");
        if (!pt::store::set_enabled(name, on)) {
            std::cerr << "error: cannot " << sub << " '" << name
                      << "' (at least one profile must stay enabled)\n";
            return 1;
        }
        std::cout << "profile " << name << (on ? " enabled" : " disabled") << "\n";
        return 0;
    }
    if (sub == "current") {
        std::string name = a.pos.size() > 1 ? a.pos[1] : get(a, "name");
        if (name.empty()) { std::cerr << "error: profile name required\n"; return 1; }
        if (!pt::store::set_current(name)) {
            std::cerr << "error: unknown or disabled profile '" << name << "'\n";
            return 1;
        }
        std::cout << "active profile: " << name << "\n";
        return 0;
    }
    if (sub == "delete" || sub == "rm") {
        std::string name = a.pos.size() > 1 ? a.pos[1] : get(a, "name");
        if (name.empty()) { std::cerr << "error: profile name required\n"; return 1; }
        if (!pt::store::remove_profile(name)) {
            std::cerr << "error: cannot delete '" << name
                      << "' (last enabled profile)\n";
            return 1;
        }
        std::cout << "profile " << name << " removed\n";
        return 0;
    }
    usage_profile();
    return 1;
}

int cmd_logout(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) {
        std::cerr << "Usage: progressive-terminal logout [--profile <name>]\n"
                     "  Forget the account in a profile (keeps the profile/proxy).\n";
        return 0;
    }
    std::string pname = get(a, "profile");
    if (pname.empty()) pname = pt::store::current_name();
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) {
        std::cerr << "error: unknown profile '" << pname << "'\n";
        return 1;
    }
    p.session.clear();
    pt::store::save_profile(p);
    std::cout << "logged out of profile " << pname << "\n";
    return 0;
}

int cmd_proxy(Args& a) {
    if (a.opt.count("help") || a.opt.count("h")) { usage_proxy(); return 0; }
    std::string sub = a.pos.empty() ? "" : a.pos[0];
    const std::string host = host_from(a);

    if (sub == "on") {
        std::string preset = a.pos.size() > 1 ? a.pos[1] : get(a, "preset");
        if (preset.empty()) { std::cerr << "error: usage: proxy on <preset>\n"; return 1; }
        pt::HttpResult r = pt::http_post_json(
            host + "/api/ttys/proxy",
            "{\"action\":\"on\",\"preset\":" + pt::json::str(preset) + "}",
            bearer_from(a));
        std::cout << r.body << "\n";
        return 0;
    }
    if (sub == "off") {
        pt::HttpResult r = pt::http_post_json(
            host + "/api/ttys/proxy", "{\"action\":\"off\"}", bearer_from(a));
        std::cout << r.body << "\n";
        return 0;
    }
    if (sub == "status") {
        pt::store::Profile p;
        pt::store::load_profile("", p);
        std::cout << "local profile : " << (p.name.empty() ? "(none)" : p.name)
                  << "  proxy=" << (p.proxy.empty() ? "(server default)" : p.proxy) << "\n";
        pt::HttpResult r = pt::http_get_json(host + "/api/ttys/proxy",
                                             bearer_from(a));
        if (r.http_status == 401)
            std::cout << "relay proxy   : (unauthorized — pass --relay-token "
                         "or set $PROGTERM_TOKEN)\n";
        else if (r.http_status == 200) std::cout << "relay proxy   : " << r.body << "\n";
        else std::cout << "relay proxy   : (relay does not expose proxy status)\n";
        return 0;
    }
    usage_proxy();
    return 1;
}

void usage() {
    std::cerr <<
        "progressive-terminal — lightweight curl-wrapper for progressive-chat (cli)\n\n"
        "Usage: progressive-terminal <command> [--profile <name>] [options]\n\n"
        "Commands:\n"
        "  render     request the ASCII UI (detect terminal size, send, print)\n"
        "  register   POST /api/ttys/register -> saves into a profile\n"
        "  session    POST /api/ttys/session  -> saves into a profile\n"
        "  input      POST /api/ttys/input    (send one line to a session)\n"
        "  sync       POST /api/ttys/sync      (poll sync state)\n"
        "  use <name> switch the active profile\n"
        "  accounts   list profiles (alias for 'profile list')\n"
        "  profile    manage profiles (create/set/enable/disable/current/delete)\n"
        "  proxy      manage the relay proxy (on <preset> | off | status)\n"
        "  logout     forget the account in a profile (keeps profile/proxy)\n\n"
        "Global / per-command:\n"
        "  --host <url>     server endpoint (or $PROGTERM_HOST / cached host)\n"
        "  --no-scan        don't auto-scan nearby ports for the relay\n"
        "  --scan-base <p>  base port for scan (default 29325)\n"
        "  --scan-range <n> scan base±n ports (default 10)\n"
        "  --profile <name> pick a profile (register/session/render/input/sync);\n"
        "                   default=active\n"
        "  --proxy <spec>   per-profile proxy: socks5://[u:p@]h:p | http://h:p | off\n"
        "  --relay-token <t> relay auth token (or $PROGTERM_TOKEN), sent as\n"
        "                   'Authorization: Bearer' — matches serve --ttys --token\n"
        "  -h, --help       this help\n\n"
        "Profiles: each profile is a container with optional account + proxy. A\n"
        "  profile may exist with no account. At least one profile is always\n"
        "  enabled. Auto-connect scans 127.0.0.1 ports around --scan-base when no\n"
        "  host is given. This is the ONLY local state (no message DB).\n";
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
    if (cmd == "profile")  return cmd_profile(a);
    if (cmd == "proxy")    return cmd_proxy(a);
    if (cmd == "logout")   return cmd_logout(a);

    std::cerr << "unknown command: " << cmd << "\n";
    usage();
    return 1;
}
