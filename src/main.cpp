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
#include "store.hpp"

#include <iostream>
#include <string>
#include <cstdlib>

#ifdef __unix__
#include <sys/stat.h>
#endif
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
    // Catch-offline is ON by default: undelivered POSTs wait in
    // ~/.config/progterm-lite/outbox until the relay answers again.
    //   PROGTERM_OUTBOX=<file>  use another file
    //   PROGTERM_OUTBOX=        (empty value) disable spooling entirely
    const char* e = std::getenv("PROGTERM_OUTBOX");
    if (!e) {
        std::string d = getenv("HOME") ? getenv("HOME") : ".";
        mkdir((d + "/.config").c_str(), 0700);
        mkdir((d + "/.config/progterm-lite").c_str(), 0700);
        return d + "/.config/progterm-lite/outbox";
    }
    return e;   // may be "" -> spooling disabled
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

// ---- lite: remember exactly ONE session id ------------------------------
// File lives next to nothing else; PROGTERM_SESSION overrides it entirely.
// Pull "session":"…" out of a relay response without a JSON parser.
std::string sess_of(const std::string& body) {
    const std::string k = "\"session\":\"";
    size_t p = body.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = body.find('"', p);
    return e == std::string::npos ? "" : body.substr(p, e - p);
}
// What does the user mean by the (optional) <session> argument?
// A name of an existing profile wins; otherwise it is a raw session id.
// With no argument the CURRENT profile is used.
// ---- tiny permanent caches (the THIN side may persist freely; the RAM-only
// rule applies to the relay) ----
std::string lite_dir() {
    std::string d = getenv("HOME") ? getenv("HOME") : ".";
    d += "/.config/progterm-lite";
    mkdir(d.c_str(), 0700);
    return d;
}
void write_cache(const std::string& name, const std::string& data) {
    std::ofstream f(lite_dir() + "/" + name, std::ios::trunc);
    if (f) f << data;
}
bool read_cache(const std::string& name, std::string& out) {
    std::ifstream f(lite_dir() + "/" + name);
    return static_cast<bool>(std::getline(f, out, '\0')) && !out.empty();
}

struct Target {
    std::string session, host, proxy, profile;
    bool from_profile = false;
};
Target resolve_target(int argc, char** argv) {
    Target t;
    const std::string arg = argc >= 3 ? argv[2] : "";
    pt::store::Profile p;
    if (!arg.empty() && pt::store::load_profile(arg, p)) {
        t = {p.session, p.host, p.proxy, p.name, true};
    } else if (!arg.empty()) {
        t.session = arg;                                   // raw session id
    } else if (pt::store::load_profile("", p)) {
        t = {p.session, p.host, p.proxy, p.name, true};
    }
    return t;
}


// Proxy of the ACTIVE profile (explicit <profile> argument wins when it
// names one). Every session request re-asserts this value.
std::string active_proxy(int argc, char** argv) {
    if (argc >= 3) {
        pt::store::Profile p;
        if (pt::store::load_profile(argv[2], p)) return p.proxy;
    }
    pt::store::Profile p;
    if (pt::store::load_profile("", p)) return p.proxy;
    return "";
}

// Session for a verb; explains the profile-aware resolution on failure.
std::string jesc(const std::string& s);

std::string need_session(int argc, char** argv) {
    const Target t = resolve_target(argc, argv);
    if (!t.session.empty()) return t.session;
    std::cerr << "no session — run: progterm-lite register <homeserver> <user>"
                 " <password> [profile]   (or pass <session|profile>)\n";
    return "";
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

// Append the profile's proxy to a finished session-request body so the
// relay re-applies it per-session (server globals stay untouched).
std::string with_proxy(std::string body, const std::string& px) {
    if (px.empty()) return body;
    const auto pos = body.rfind('}');
    body.insert(pos, ",\"proxy\":\"" + jesc(px) + "\"");
    return body;
}

std::string render_body(const std::string& session, const std::string& room);

// progterm term <session> [room] — the remote-terminal loop: every stdin
// line is delivered to the full client's REPL, then the refreshed screen
// comes back as plain text and is printed verbatim.
int term_loop(const std::string& host, const std::string& bearer,
              const std::string& session, const std::string& room,
              const std::string& proxy) {
    const std::string in_url = host + "/api/ttys/input";
    const std::string rd_url = host + "/api/ttys/render";
    auto frame_body = [&] { return with_proxy(render_body(session, room), proxy); };

    pt::http_post_plain(rd_url, frame_body(), bearer);  // first paint
    std::cout << "term> " << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == ":q" || line == "/quit") return 0;
        if (!line.empty()) {
            const std::string ibody =
                "{\"session\":\"" + jesc(session) +
                "\",\"input\":\"" + jesc(line) + "\"}";
            const pt::HttpResult ir = pt::http_post_json(
                in_url, with_proxy(ibody, proxy), bearer);
            if (ir.http_status == 0)
                outbox_record("api/ttys/input", ibody);
        }
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
std::string render_body(const std::string& session, const std::string& room) {
    int c, r;
    term_size(c, r);
    std::string b = "{\"session\":\"" + jesc(session) + "\"" +
                    ",\"term\":{\"cols\":" + std::to_string(c) +
                    ",\"rows\":" + std::to_string(r) + "}";
    if (!room.empty()) b += ",\"room\":\"" + jesc(room) + "\"";
    return b + ",\"view\":\"static\"}";
}

// ---- queries caught BEFORE the wire -------------------------------------
// Special verbs: either purely local (help) or formatters around one call,
// so the user never writes JSON for the routine cases.

// progterm profile <action> ... — containers for sessions (+ per-profile
// proxy applied at register time). Ported from the sugar edition.
int cmd_profile(int argc, char** argv) {
    const std::string sub = argc >= 3 ? argv[2] : "list";
    auto nm = [&](int idx) { return argc > idx ? argv[idx] : ""; };

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
    if (sub == "create" || sub == "set") {
        const std::string name = nm(3);
        if (name.empty()) { std::cerr << "profile name required\n"; return 1; }
        pt::store::Profile p;
        if (!pt::store::load_profile(name, p)) { p.name = name; p.enabled = true; }
        if (argc >= 5) p.proxy = argv[4];
        pt::store::save_profile(p);
        std::cout << "profile " << name << " saved\n";
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        const std::string name = nm(3);
        if (!pt::store::set_enabled(name, sub == "enable")) {
            std::cerr << "cannot " << sub << " '" << name
                      << "' (last enabled?)\n";
            return 1;
        }
        std::cout << "profile " << name << (sub == "enable" ? " enabled" : " disabled") << "\n";
        return 0;
    }
    if (sub == "current") {
        const std::string name = nm(3);
        if (!pt::store::set_current(name)) {
            std::cerr << "unknown or disabled profile '" << name << "'\n";
            return 1;
        }
        std::cout << "active profile: " << name << "\n";
        return 0;
    }
    if (sub == "delete" || sub == "rm") {
        const std::string name = nm(3);
        if (!pt::store::remove_profile(name)) {
            std::cerr << "cannot delete '" << name << "' (last enabled?)\n";
            return 1;
        }
        std::cout << "profile " << name << " removed\n";
        return 0;
    }
    if (sub == "export" || sub == "import") {
        const std::string file = nm(3);
        if (file.empty()) {
            std::cerr << "usage: progterm profile " << sub << " <file>\n";
            return 1;
        }
        const auto ps = pt::store::list_profiles();
        if (sub == "export") {
            if (ps.empty()) { std::cerr << "nothing to export\n"; return 1; }
            std::ofstream f(file, std::ios::trunc);
            f << "current " << pt::store::current_name() << "\n";
            for (const auto& p : ps)
                f << "profile " << p.name << "\n"
                  << "enabled " << (p.enabled ? "true" : "false") << "\n"
                  << "proxy "   << p.proxy  << "\n"
                  << "host "    << p.host   << "\n"
                  << "session " << p.session << "\n";
            f.close();
#ifdef __unix__
            chmod(file.c_str(), 0600);   // the bundle carries session ids
#endif
            std::cout << "exported " << ps.size() << " profile(s) -> "
                      << file << "\n";
            return 0;
        }
        // import: upsert by name; everything absent stays untouched
        std::ifstream f(file);
        if (!f) { std::cerr << "cannot open " << file << "\n"; return 1; }
        std::string line, cur_name;
        pt::store::Profile cur;
        bool have = false;
        int n = 0;
        std::string want_current;
        auto commit = [&] {
            if (!have) return;
            pt::store::save_profile(cur);
            ++n;
            have = false;
        };
        while (std::getline(f, line)) {
            if (line.rfind("profile ", 0) == 0) {
                commit();
                cur = pt::store::Profile();
                cur.name = line.substr(8);
                cur.enabled = true;
                have = true;
            } else if (line.rfind("current ", 0) == 0) {
                want_current = line.substr(8);
            } else if (have && !line.empty()) {
                const size_t sp = line.find(' ');
                if (sp == std::string::npos) continue;
                const std::string k = line.substr(0, sp);
                const std::string v = line.substr(sp + 1);
                if (k == "enabled") cur.enabled = (v != "false");
                else if (k == "proxy")  cur.proxy  = v;
                else if (k == "host")   cur.host   = v;
                else if (k == "session")cur.session= v;
            }
        }
        commit();
        if (!want_current.empty()) pt::store::set_current(want_current);
        std::cout << "imported " << n << " profile(s)\n";
        return 0;
    }
    std::cerr << "unknown profile action\n";
    return 1;
}

// progterm logout [profile] — forget the session, keep the container.
int cmd_logout(int argc, char** argv) {
    std::string pname = argc >= 3 ? argv[2] : pt::store::current_name();
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) {
        std::cerr << "unknown profile '" << pname << "'\n";
        return 1;
    }
    p.session.clear();
    pt::store::save_profile(p);
    std::cout << "logged out of profile " << pname << "\n";
    return 0;
}

int cmd_last(const std::string& host, const std::string& bearer) {
    const pt::HttpResult r =
        pt::http_get_json(host + "/api/ttys/session/last", bearer);
    if (r.http_status != 200) { std::cout << r.body << "\n"; return 1; }
    const std::string sid = sess_of(r.body);
    if (!sid.empty()) {
        pt::store::Profile p;
        if (pt::store::load_profile("", p)) {
            p.session = sid; p.enabled = true;
            pt::store::save_profile(p);
        }
    }
    std::cout << sid << "\n";
    return 0;
}

// progterm register <homeserver> <user> <password> — create an account,
// remember the returned session id, print it. No JSON from the user.
int cmd_register(const std::string& host, const std::string& bearer,
                 int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: progterm register <homeserver> <user> <password>"
                  << " [profile]\n";
        return 1;
    }
    std::string pname = argc >= 6 ? argv[5] : "";
    if (pname.empty()) { pt::store::ensure_default_profile(); pname = pt::store::current_name(); }
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) { p.name = pname; p.enabled = true; }
    p.enabled = true;

    const std::string body =
        "{\"homeserver\":\"" + jesc(argv[2]) + "\""
        ",\"username\":\"" + jesc(argv[3]) + "\""
        ",\"password\":\"" + jesc(argv[4]) + "\""
        ",\"reg_token\":\"\",\"proxy\":\"" + jesc(p.proxy) + "\"}";
    if (getenv("PROGTERM_DEBUG")) std::cerr << "BODY> " << body << "\n";
    const pt::HttpResult r =
        pt::http_post_json(host + "/api/ttys/register", body, bearer);
    if (r.http_status != 200) { std::cout << r.body << "\n"; return 1; }
    const std::string sid = sess_of(r.body);
    if (sid.empty()) { std::cerr << "no session in response\n"; return 1; }
    p.session = sid; p.host = host;
    pt::store::save_profile(p);
    pt::store::set_current(p.name);
    std::cout << sid << "\n";
    return 0;
}

int cmd_sync(const std::string& host, const std::string& bearer, int argc,
             char** argv) {
    const std::string ses = need_session(argc, argv);
    if (ses.empty()) return 1;
    const std::string body = "{\"session\":\"" + jesc(ses) + "\"}";
    const pt::HttpResult r = pt::http_post_plain(
        host + "/api/ttys/sync", body, bearer);
    if (r.http_status == 0)
        return outbox_record("api/ttys/sync", body) ? 0 : 2;
    std::cout << r.body << "\n";
    return r.http_status == 200 ? 0 : 1;
}

// proxy <spec|off> [profile] — THIN-SIDE setting: stored in the profile
// container and sent with EVERY session request; the relay applies it
// per-session and never touches its own global config.
int cmd_proxy(int argc, char** argv) {
    const std::string spec = argc >= 3 ? argv[2] : "";
    std::string pname = argc >= 4 ? argv[3] : "";
    if (pname.empty()) { pt::store::ensure_default_profile(); pname = pt::store::current_name(); }
    if (spec.empty()) {  // status: local value + relay global (informational)
        pt::store::Profile p;
        pt::store::load_profile(pname, p);
        std::cout << pname << ".proxy = "
                  << (p.proxy.empty() ? "(off)" : p.proxy) << "\n";
        const pt::HttpResult r =
            pt::http_get_json(host_from() + "/api/ttys/proxy", bearer_from());
        std::cout << "full-client global: " << r.body << "\n";
        return 0;
    }
    pt::store::Profile p;
    if (!pt::store::load_profile(pname, p)) { p.name = pname; p.enabled = true; }
    p.proxy = (spec == "off") ? "" : spec;
    pt::store::save_profile(p);
    std::cout << pname << ".proxy = "
              << (p.proxy.empty() ? "(off)" : p.proxy) << "\n";
    return 0;
}

void usage() {
    // The face of the client is the face of the FULL client: fetch its
    // catalog from the relay instead of storing a copy here.
    const pt::HttpResult r = pt::http_get_plain(
        host_from() + "/api/ttys/usage", bearer_from());
    if (r.http_status == 200) {
        write_cache("usage.cache", r.body);
        std::cout << r.body << std::flush;
        return;
    }
    std::string cached;
    if (read_cache("usage.cache", cached)) {
        std::cout << "[cached]\n" << cached << std::flush;
        return;
    }
    std::cout << "progterm-lite — special proxy to the full client\n";
    if (r.http_status == 401)
        std::cout << "\n[!] relay requires a token: export PROGTERM_TOKEN=<t>"
                     " (serve --ttys --token <t>)\n";
    else if (r.http_status == 404)
        std::cout << "\n[!] relay is TOO OLD: it has no /api/ttys/usage."
                     " Update the full client (v0.5.4+).\n";
    else
        std::cout << "\n[!] relay unreachable — start"
                     " 'progressive-cli serve --ttys'.\n";
}

}  // namespace

// progterm raw <path> [json-body] — explicit verbatim HTTP escape hatch
// (for scripting against endpoints that have no verb yet).
int cmd_raw(const std::string& host, const std::string& bearer, int argc,
            char** argv) {
    if (argc < 3) { std::cerr << "usage: progterm raw <path> [json-body]\n"; return 1; }
    std::string path = argv[2];
    if (!path.empty() && path[0] == '/') path.erase(0, 1);
    const std::string body = argc >= 4 ? argv[3] : "";
    const pt::HttpResult r = body.empty()
        ? pt::http_get_json(host + "/" + path, bearer)
        : pt::http_post_json(host + "/" + path, body, bearer);
    if (r.http_status == 0)
        return (!body.empty() && outbox_record(path, body)) ? 0 : 2;
    std::cout << r.body << "\n";
    return (r.http_status >= 200 && r.http_status < 300) ? 0 : 1;
}

int main(int argc, char** argv) {
    // A line beginning with '/' is ALWAYS a command of the FULL client's
    // REPL — deliver verbatim, never catch locally.
    const bool slashed = argc >= 2 && argv[1][0] == '/';

    if (argc < 2 || argv[1] == std::string("help")) { usage(); return slashed ? 9 : 0; }
    const std::string arg1 = argv[1];

    if (!slashed) {
        if (arg1 == "raw")      return cmd_raw(host_from(), bearer_from(), argc, argv);
        if (arg1 == "register") return cmd_register(host_from(), bearer_from(), argc, argv);

        const Target tgt = resolve_target(argc, argv);
        const std::string host = !tgt.host.empty() ? tgt.host : host_from();
        const std::string bearer = bearer_from();
        outbox_flush(host, bearer);   // deliver anything spooled earlier

        if (arg1 == "last")     return cmd_last(host, bearer);
        if (arg1 == "sync")     return cmd_sync(host, bearer, argc, argv);
        if (arg1 == "proxy")    return cmd_proxy(argc, argv);
        if (arg1 == "profile")  return cmd_profile(argc, argv);
        if (arg1 == "logout")   return cmd_logout(argc, argv);
        if (arg1 == "render") {
            const std::string ses = need_session(argc, argv);
            if (ses.empty()) return 1;
            const std::string room = argc >= 4 ? argv[3] : "";
            const std::string rb = render_body(ses, room);
            const pt::HttpResult fr =
                pt::http_post_plain(host + "/api/ttys/render", rb, bearer);
            if (fr.http_status == 200) {
                write_cache("last_frame", fr.body);
                std::cout << "\x1b[2J\x1b[H" << fr.body;
                return 0;
            }
            if (fr.http_status == 0) {
                std::string last;
                if (read_cache("last_frame", last))
                    std::cout << "\x1b[2J\x1b[H" << last
                              << "\n[offline] showing last known screen\n";
                else return outbox_record("api/ttys/render", rb) ? 0 : 2;
                return 0;
            }
            std::cout << fr.body << "\n";
            return 1;
        }
        if (arg1 == "term") {
            const std::string ses = need_session(argc, argv);
            if (ses.empty()) return 1;
            return term_loop(host, bearer, ses, argc >= 4 ? argv[3] : "",
                            active_proxy(argc, argv));
        }
    }

    // ---- THE PROXY ESSENCE ----
    // Anything else is a LINE FOR THE FULL CLIENT: chat text or one of its
    // own REPL commands ("/join …", "/help", "dump …"). Deliver verbatim,
    // then print what the full client answered (refreshed screen).
    const Target tgt = resolve_target(argc, argv);
    const std::string host = !tgt.host.empty() ? tgt.host : host_from();
    const std::string bearer = bearer_from();
    const std::string ses = need_session(2, argv);   // no <session> slot here
    if (ses.empty()) return 1;

    std::string line = arg1;
    for (int i = 2; i < argc; ++i) line += std::string(" ") + argv[i];
    const std::string ibody = with_proxy(
        "{\"session\":\"" + jesc(ses) +
        "\",\"input\":\"" + jesc(line) + "\"}", active_proxy(argc, argv));
    const pt::HttpResult ir =
        pt::http_post_json(host + "/api/ttys/input", ibody, bearer);
    if (ir.http_status == 0 && outbox_record("api/ttys/input", ibody)) {
        std::cout << "(offline — будет доставлено при связи)\n";
        return 0;
    }
    const pt::HttpResult fr = pt::http_post_plain(
        host + "/api/ttys/render", render_body(ses, ""), bearer);
    if (fr.http_status == 200) {
        write_cache("last_frame", fr.body);
        std::cout << "\x1b[2J\x1b[H" << fr.body;
    } else {
        std::string last;
        if (read_cache("last_frame", last))
            std::cout << "\x1b[2J\x1b[H" << last
                      << "\n[offline] showing last known screen\n";
        else std::cout << fr.body << "\n";
    }
    return 0;
}
