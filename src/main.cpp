#include "http.hpp"
#include "json.hpp"
#include "proto.hpp"
#include "render.hpp"
#include "store.hpp"
#include "discover.hpp"
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

// int-valued --flag with fallback (scan window, forced cols/rows).
int num_opt(const Args& a, const char* k, int def) {
    return a.opt.count(k) ? std::stoi(get(a, k)) : def;
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
        const int base  = num_opt(a, "scan-base", 29325);
        const int range = num_opt(a, "scan-range", 10);
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

// The whole register/session tail in one place: resolve the target profile,
// POST the body, echo credential fields, store+activate a returned session.
int post_auth(Args& a, const std::string& host, const char* path,
              const std::string& body,
              std::initializer_list<const char*> fields) {
    // Target profile; created enabled when missing so register works on a
    // fresh install.
    std::string pname = get(a, "profile");
    if (pname.empty()) { pt::store::ensure_default_profile(); pname = pt::store::current_name(); }
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) { p.name = pname; p.enabled = true; }

    pt::HttpResult r = pt::http_post_json(host + path, body, bearer_from(a));
    if (!r.ok()) {
        std::string err;
        if (pt::json::get_string(r.body, "error", err))
            std::cerr << "error: " << err << "\n";
        else
            std::cerr << "error: HTTP " << r.http_status << "\n";
        return 1;
    }
    for (const char* f : fields) {
        std::string v;
        if (pt::json::get_string(r.body, f, v)) std::cout << v << "\n";
    }
    std::string sid;
    if (pt::json::get_string(r.body, "session", sid) && !sid.empty()) {
        p.session = sid; p.host = host; p.proxy = get(a, "proxy");
        pt::store::save_profile(p);
        pt::store::set_current(p.name);
    }
    return 0;
}

// Resolve the profile for session-carrying commands; fails when it has none.
bool need_session(const Args& a, pt::store::Profile& p) {
    p = resolve_profile(a);
    if (!p.session.empty()) return true;
    std::cerr << "error: no session (register into a profile first)\n";
    return false;
}

// ---- per-command help ---------------------------------------------------

// ---- commands ------------------------------------------------------------

int cmd_register(Args& a) {
    const std::string host = host_from(a);
    return post_auth(a, host, "/api/ttys/register",
        pt::proto::registerBody(get(a, "homeserver"), get(a, "username"),
                                get(a, "password"), get(a, "reg-token"),
                                get(a, "proxy")),
        {"session", "user_id", "access_token", "device_id"});
}

int cmd_session(Args& a) {
    const std::string host = host_from(a);
    return post_auth(a, host, "/api/ttys/session",
        pt::proto::sessionBody(get(a, "homeserver"), get(a, "user"),
                               get(a, "token"), get(a, "device"),
                               get(a, "proxy")),
        {"session", "key"});
}

int cmd_input(Args& a) {
    pt::store::Profile p;
    if (!need_session(a, p)) return 1;
    pt::http_post_json(p.host + "/api/ttys/input",
                       pt::proto::inputBody(p.session, a.opt["text"]),
                       bearer_from(a));
    return 0;
}

int cmd_sync(Args& a) {
    pt::store::Profile p;
    if (!need_session(a, p)) return 1;
    pt::HttpResult r = pt::http_post_json(p.host + "/api/ttys/sync",
                                          pt::proto::syncBody(p.session),
                                          bearer_from(a));
    std::cout << r.body << "\n";
    return 0;
}

int cmd_render(Args& a) {
    pt::store::Profile p;
    if (!need_session(a, p)) return 1;
    const std::string frame = pt::request_frame(
        p.host, p.session, get(a, "room"), a.opt.count("static") > 0,
        num_opt(a, "cols", 0), num_opt(a, "rows", 0), bearer_from(a));
    if (frame.rfind("error:", 0) == 0) { std::cerr << frame << "\n"; return 1; }
    std::cout << frame;
    return 0;
}

// Positional profile name for `profile <action> <name>`; errors when absent.
bool need_name(const Args& a, std::string& name) {
    name = a.pos.size() > 1 ? a.pos[1] : get(a, "name");
    if (!name.empty()) return true;
    std::cerr << "error: profile name required\n";
    return false;
}

int cmd_profile(Args& a) {
    std::string sub = a.pos.empty() ? "" : a.pos[0];
    if (sub == "list" || sub.empty()) {
        std::cout << "profiles:\n";
        const std::string cur = pt::store::current_name();
        for (const auto& p : pt::store::list_profiles())
            std::cout << (p.name == cur ? "* " : "  ") << p.name
                      << (p.enabled ? "  [enabled]" : "  [disabled]")
                      << "  proxy=" << (p.proxy.empty() ? "(server default)" : p.proxy)
                      << "  session=" << (p.session.empty() ? "(none)" : p.session)
                      << "\n";
        return 0;
    }

    std::string name;
    if (sub == "create" || sub == "set") {
        if (!need_name(a, name)) return 1;
        pt::store::Profile p;
        if (!pt::store::load_profile(name, p)) { p.name = name; p.enabled = true; }
        if (a.opt.count("proxy")) p.proxy = get(a, "proxy");
        pt::store::save_profile(p);
        std::cout << "profile " << name << " saved (proxy="
                  << (p.proxy.empty() ? "server default" : p.proxy) << ")\n";
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        if (!need_name(a, name)) return 1;
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
        if (!need_name(a, name)) return 1;
        if (!pt::store::set_current(name)) {
            std::cerr << "error: unknown or disabled profile '" << name << "'\n";
            return 1;
        }
        std::cout << "active profile: " << name << "\n";
        return 0;
    }
    if (sub == "delete" || sub == "rm") {
        if (!need_name(a, name)) return 1;
        if (!pt::store::remove_profile(name)) {
            std::cerr << "error: cannot delete '" << name
                      << "' (last enabled profile)\n";
            return 1;
        }
        std::cout << "profile " << name << " removed\n";
        return 0;
    }
    std::cerr << "error: unknown profile action (see: profile --help)\n";
    return 1;
}

int cmd_logout(Args& a) {
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
    std::string sub = a.pos.empty() ? "" : a.pos[0];
    const std::string host = host_from(a);

    if (sub == "on" || sub == "off") {
        std::string preset = a.pos.size() > 1 ? a.pos[1] : get(a, "preset");
        if (sub == "on" && preset.empty()) {
            std::cerr << "error: usage: proxy on <preset>\n";
            return 1;
        }
        pt::HttpResult r = pt::http_post_json(
            host + "/api/ttys/proxy",
            sub == "on" ? pt::proto::proxyOnBody(preset) : pt::proto::proxyOffBody(),
            bearer_from(a));
        std::cout << r.body << "\n";
        return 0;
    }
    if (sub == "status") {
        pt::store::Profile p = resolve_profile(a);
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
    std::cerr << "error: usage: proxy <on|off|status> [args]\n";
    return 1;
}

// ---- dispatch table (drives both routing and the global help) ------------

struct Command {
    const char* name;
    const char* brief;   // one-liner: shown in the global help and used as
                         // the fallback -h text when `detail` is null
    int (*run)(Args&);
    void (*detail)();    // extended help; may be null
};

const Command kCommands[] = {
    {"render",  "[--profile <n>] [--static] [--room <id>] [--cols/--rows <n>]", cmd_render,   nullptr},
    {"register","--homeserver <url> --username <u> --password <p> [--reg-token <t>] [--proxy <spec>]",  cmd_register, nullptr},
    {"session", "--homeserver <url> --user <@id> --token <t> --device <d> [--proxy <spec>]",            cmd_session,  nullptr},
    {"input",   "send one input line to the session (--text <line>)",         cmd_input,    nullptr},
    {"sync",    "poll the relay's sync state",                                cmd_sync,     nullptr},
    {"profile", "create|set <name> [--proxy <spec>] | enable|disable | current | delete | list",     cmd_profile,  nullptr},
    {"proxy",   "on <preset> | off | status   (relay-side proxy presets)",          cmd_proxy,    nullptr},
    {"logout",  "forget the account in a profile (keeps profile/proxy)",      cmd_logout,   nullptr},
};

void usage() {
    std::cerr <<
        "progressive-terminal — lightweight curl-wrapper for progressive-chat (cli)\n\n"
        "Usage: progressive-terminal <command> [options]\n\nCommands:\n";
    for (const auto& c : kCommands)
        std::cerr << "  " << c.name << "  " << c.brief << "\n";
    std::cerr <<
        "\nGlobal flags:\n"
        "  --host <url> | $PROGTERM_HOST | --profile <name> | --proxy <spec> |\n"
        "  --relay-token <t> ($PROGTERM_TOKEN) | --no-scan | --scan-base/-range\n"
        "  Full reference and examples: README.md.\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    const std::string arg1 = argv[1];
    if (arg1 == "help" || arg1 == "-h") { usage(); return 0; }
    Args a = parse(argc, argv);

    for (const auto& c : kCommands) {
        if (arg1 != c.name) continue;
        if (a.opt.count("help") || a.opt.count("h")) {
            if (c.detail) c.detail();
            else std::cerr << c.name << " — " << c.brief << "\n";
            return 0;
        }
        return c.run(a);
    }

    std::cerr << "unknown command: " << arg1 << "\n";
    usage();
    return 1;
}
