// progterm-lite — special proxy to the full client (progressive-cli).
//
// ANY string you type is delivered verbatim as input to the full client's
// REPL — chat text or one of its own commands ("/open …", "/help") — and
// what you see back is what the full client answered: its refreshed screen.
//
// A leading '/' is ALWAYS a full-client command (never caught locally).
// A few verbs are caught before the wire for convenience; everything else
// is proxied. Offline, POSTs are spooled to ~/.config/progterm-lite/outbox
// and delivered on the next successful contact.
//
// Layout-independent typing: PROGTERM_LAYOUT=ru|de|fr|es|it|br|dvorak|colemak
// remaps entered lines from that layout's printed characters back to US keys.

#include "net.hpp"
#include "store.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cctype>

#ifdef __unix__
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace {

std::string host_from() {
    if (const char* e = std::getenv("PROGTERM_HOST")) return e;
    const std::string found = pt::discover_relay();
    return found.empty() ? "http://127.0.0.1:29325" : found;
}

std::string bearer_from() {
    const char* e = std::getenv("PROGTERM_TOKEN");
    return e ? e : "";
}

// Escape a raw line as a JSON string value (no surrounding quotes).
std::string jesc(const std::string& s) {
    std::string o;
    o.reserve(s.size());
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

// Layout recovery for typed lines: PROGTERM_LAYOUT=ru remaps ЙЦУКЕН
// characters back to US keys (forgot-to-switch-layout helper). More
// layouts can be added to the same table.
std::string layout_fix_line(const std::string& in) {
    const char* e = std::getenv("PROGTERM_LAYOUT");
    if (!e || std::string(e) != "ru") return in;
    static const char* R[] =
        {"й","ц","у","к","е","н","г","ш","щ","з","х","ъ",
         "ф","ы","в","а","п","р","о","л","д","ж","э",
         "я","ч","с","м","и","т","ь","б","ю","ё"};
    static const char* E[] =
        {"q","w","e","r","t","y","u","i","o","p","[","]",
         "a","s","d","f","g","h","j","k","l",";","'",
         "z","x","c","v","b","n","m",",",".","`"};
    std::string o;
    o.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const unsigned char lead = static_cast<unsigned char>(in[i]);
        if (lead == 0xD0 || lead == 0xD1) {
            const std::string k = in.substr(i, 2);
            bool done = false;
            for (size_t t = 0; t < sizeof(R) / sizeof(R[0]); ++t)
                if (k == R[t]) { o += E[t]; done = true; break; }
            i += done ? 2 : 2;
        } else { o += in[i]; ++i; }
    }
    return o;
}

// Unconditional RU->US ЙЦУКЕН transliteration (used by the CLI recovery).
std::string ru_to_us(const std::string& in) {
    static const char* R[] =
        {"й","ц","у","к","е","н","г","ш","щ","з","х","ъ",
         "ф","ы","в","а","п","р","о","л","д","ж","э",
         "я","ч","с","м","и","т","ь","б","ю","ё"};
    static const char* E[] =
        {"q","w","e","r","t","y","u","i","o","p","[","]",
         "a","s","d","f","g","h","j","k","l",";","'",
         "z","x","c","v","b","n","m",",",".","`"};
    std::string o;
    o.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const unsigned char lead = static_cast<unsigned char>(in[i]);
        if (lead == 0xD0 || lead == 0xD1) {
            const std::string k = in.substr(i, 2);
            bool done = false;
            for (size_t t = 0; t < sizeof(R)/sizeof(R[0]); ++t)
                if (k == R[t]) { o += E[t]; done = true; break; }
            i += done ? 2 : 2;
        } else { o += in[i]; ++i; }
    }
    return o;
}

bool contains_cyrillic(const char* s) {
    for (; *s; ++s)
        if ((static_cast<unsigned char>(*s) & 0xF0) == 0xD0) return true;
    return false;
}

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

std::string outbox_path() {
    // ON by default. PROGTERM_OUTBOX=<file> redirects; empty value disables.
    const char* e = std::getenv("PROGTERM_OUTBOX");
    if (!e) return lite_dir() + "/outbox";
    return e;
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
    std::ofstream(p, std::ios::trunc);
    for (const auto& l : pending) {
        const size_t tab = l.find('\t');
        if (tab == std::string::npos) continue;
        const pt::HttpResult r = pt::post(
            host + "/" + l.substr(0, tab), l.substr(tab + 1), bearer);
        if (r.status == 0)
            std::ofstream(p, std::ios::app) << l << '\n';
    }
}
bool outbox_record(const std::string& path, const std::string& body) {
    const std::string p = outbox_path();
    if (p.empty()) return false;
    std::ofstream(p, std::ios::app) << path << '\t' << body << '\n';
    std::cerr << "(offline — будет доставлено при связи)\n";
    return true;
}

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

std::string render_body(const std::string& session, const std::string& room) {
    int c, r;
    term_size(c, r);
    std::string b = "{\"session\":\"" + jesc(session) + "\"" +
                    ",\"term\":{\"cols\":" + std::to_string(c) +
                    ",\"rows\":" + std::to_string(r) + "}";
    if (!room.empty()) b += ",\"room\":\"" + jesc(room) + "\"";
    return b + ",\"view\":\"static\"}";
}

struct Target {
    std::string session, host, proxy, profile;
};
Target resolve_target(int argc, char** argv) {
    Target t;
    const std::string arg = argc >= 3 ? argv[2] : "";
    pt::store::Profile p;
    if (!arg.empty() && pt::store::load(arg, p))
        t = {p.session, p.host, p.proxy, p.name};
    else if (!arg.empty())
        t.session = arg;                                   // raw session id
    else if (pt::store::load("", p))
        t = {p.session, p.host, p.proxy, p.name};
    return t;
}

struct SessPx { std::string ses, px; bool from_profile=false; };
SessPx sess_px(int argc, char** argv) {
    const Target t = resolve_target(argc, argv);
    if (!t.session.empty()) return {t.session, t.proxy, true};
    pt::store::Profile p;
    if (pt::store::load("", p) && !p.session.empty())
        return {p.session, t.proxy, true};
    return {};
}

// Pull "session":"…" out of a relay response without a JSON parser.
std::string sess_of(const std::string& body) {
    const std::string k = "\"session\":\"";
    size_t p = body.find(k);
    if (p == std::string::npos) return "";
    p += k.size();
    size_t e = body.find('"', p);
    return e == std::string::npos ? "" : body.substr(p, e - p);
}

void usage() {
    const pt::HttpResult r = pt::get_plain(
        host_from() + "/api/ttys/usage", bearer_from());
    if (r.status == 200) { std::cout << r.body << std::flush; return; }
    std::cout << "progterm-lite — special proxy to the full client\n\n"
                 "verbs: register|session <hs> <u> <p|token> <dev?> [profile]\n"
                 "  last | sync|render|term [ses|profile] [room] | logout\n"
                 "  proxy [<spec>|off|status] | profile <action>… | raw …\n";
}

// One shared screen refresh: POST render (with proxy), cache on success,
// fall back to the last known frame when offline. Returns true if a live
// or cached frame was printed.
bool fetch_frame(const std::string& host, const std::string& bearer,
                 const std::string& ses, const std::string& room,
                 const std::string& px) {
    const std::string rb = with_proxy(render_body(ses, room), px);
    const pt::HttpResult fr =
        pt::post_plain(host + "/api/ttys/render", rb, bearer);
    if (fr.status == 200) {
        write_cache("last_frame", fr.body);
        std::cout << "\x1b[2J\x1b[H" << fr.body;
        return true;
    }
    std::string last;
    if (fr.status == 0 && read_cache("last_frame", last)) {
        std::cout << "\x1b[2J\x1b[H" << last
                  << "\n[offline] showing last known screen\n";
        return true;
    }
    if (fr.status != 0) std::cout << fr.body << "\n";
    return false;
}

// Interactive remote-terminal loop: read line → deliver → show screen.
int term_loop(const std::string& host, const std::string& bearer,
              const std::string& session, const std::string& room,
              const std::string& proxy) {
    const std::string in_url = host + "/api/ttys/input";
    fetch_frame(host, bearer, session, room, proxy);   // first paint
    std::cout << "> " << std::flush;
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == ":q" || line == ":quit" || line == "/quit") break;
        if (line.empty()) { std::cout << "> " << std::flush; continue; }
        line = layout_fix_line(line);
        const std::string ibody = with_proxy(
            "{\"session\":\"" + jesc(session) +
            "\",\"input\":\"" + jesc(line) + "\"}", proxy);
        const pt::HttpResult ir = pt::post(in_url, ibody, bearer);
        if (ir.status == 0) outbox_record("api/ttys/input", ibody);
        fetch_frame(host, bearer, session, room, proxy);
        std::cout << "> " << std::flush;
    }
    return 0;
}

// ---- commands ----
// Persist the returned session id into the profile and activate it
// (shared tail of register/session).
void remember(const std::string& host, const std::string& sid,
              pt::store::Profile p) {
    p.session = sid; p.host = host;
    pt::store::save(p);
    pt::store::set_current(p.name);
}

// Current-or-named profile, created enabled when missing.
pt::store::Profile target_profile(int argc, char** argv, int idx) {
    std::string pname = argc > idx ? argv[idx] : "";
    if (pname.empty()) { pt::store::ensure_default(); pname = pt::store::current(); }
    pt::store::Profile p;
    if (!pt::store::load(pname, p)) { p.name = pname; p.enabled = true; }
    return p;
}

int cmd_register(const std::string& host, const std::string& bearer,
                 int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: progterm-lite register <homeserver> <user>"
                     " <password> [profile]\n";
        return 1;
    }
    pt::store::Profile p = target_profile(argc, argv, 5);

    const std::string body = with_proxy(
        "{\"homeserver\":\"" + jesc(argv[2]) +
        "\",\"username\":\""   + jesc(argv[3]) +
        "\",\"password\":\""   + jesc(argv[4]) +
        "\",\"reg_token\":\"\",\"proxy\":\"" + jesc(p.proxy) + "\"}", "");

    const pt::HttpResult r =
        pt::post(host + "/api/ttys/register", body, bearer);
    if (r.status != 200) { std::cout << r.body << "\n"; return 1; }
    const std::string sid = sess_of(r.body);
    if (sid.empty()) { std::cerr << "no session in response\n"; return 1; }
    remember(host, sid, p);
    std::cout << sid << "\n";
    return 0;
}

// session <hs> <user> <token> <device> [profile] — attach an EXISTING
// account (no registration). The returned session id is remembered.
int cmd_session(const std::string& host, const std::string& bearer,
                int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "usage: progterm-lite session <homeserver> <user>"
                     " <token> <device> [profile]\n";
        return 1;
    }
    pt::store::Profile p = target_profile(argc, argv, 6);
    p.enabled = true;

    const std::string b2 = "{\"account\":{\"homeserver\":\"" + jesc(argv[2]) +
        "\",\"user_id\":\""     + jesc(argv[3]) +
        "\",\"access_token\":\"" + jesc(argv[4]) +
        "\",\"device_id\":\""   + jesc(argv[5]) +
        "\",\"proxy\":\""       + jesc(p.proxy) + "\"}}";

    const pt::HttpResult r =
        pt::post(host + "/api/ttys/session", b2, bearer);
    if (r.status != 200) { std::cout << r.body << "\n"; return 1; }
    const std::string sid = sess_of(r.body);
    if (sid.empty()) { std::cerr << "no session in response\n"; return 1; }
    remember(host, sid, p);
    std::cout << sid << "\n";
    return 0;
}
int cmd_last(const std::string& host, const std::string& bearer) {
    const pt::HttpResult r =
        pt::get(host + "/api/ttys/session/last", bearer);
    if (r.status != 200) { std::cout << r.body << "\n"; return 1; }
    const std::string sid = sess_of(r.body);
    if (!sid.empty()) {
        pt::store::Profile p;
        if (pt::store::load("", p)) {
            p.session = sid;
            pt::store::save(p);
        }
    }
    std::cout << sid << "\n";
    return 0;
}

int cmd_sync(const std::string& host, const std::string& bearer,
             int argc, char** argv) {
    const std::string ses = sess_px(argc, argv).ses;
    if (ses.empty()) return 1;
    const std::string px = sess_px(argc, argv).px;
    const std::string body = with_proxy(
        "{\"session\":\"" + jesc(ses) + "\"}", px);
    const pt::HttpResult r = pt::post_plain(
        host + "/api/ttys/sync", body, bearer);
    if (r.status == 0)
        return outbox_record("api/ttys/sync", body) ? 0 : 2;
    std::cout << r.body << "\n";
    return r.status == 200 ? 0 : 1;
}

// proxy <spec|off|status> [profile] — THIN-SIDE setting stored in the
// container and re-asserted on every request. 'status' mirrors the full
// client's own `proxy status` output byte-for-byte.
// proxy status [profile]                    — показать текущее состояние
// proxy off [profile]                       — отключить прокси в профиле
// proxy on <name|N> [profile]               — включить пресет с релея
// proxy <socks5|http>://host:port [profile] — явная спека
int cmd_proxy(int argc, char** argv) {
    auto host = host_from();
    auto bearer = bearer_from();
    const std::string sub = argc >= 3 ? argv[2] : "";

    // --- status ---
    if (sub == "status") {
        auto pn = argc >= 4 ? argv[3] : pt::store::current();
        if (pn.empty()) pn = "default";
        pt::store::Profile p;
        pt::store::load(pn, p);
        std::cout << "profile:      " << pn
                  << "\nproxy:        "
                  << (p.proxy.empty() ? "(direct)" : p.proxy) << "\n";
        const pt::HttpResult r =
            pt::get_plain(host + "/api/ttys/proxy", bearer);
        if (r.status == 200) {
            std::cout << r.body << std::flush;
        } else {
            std::cout << "relay global: (unreachable)\n";
        }
        return 0;
    }

    // --- определить целевой профиль ---
    std::string pname = argc >= 4 ? argv[3] : "";
    if (pname.empty()) { pt::store::ensure_default(); pname = pt::store::current(); }
    pt::store::Profile p;
    if (!pt::store::load(pname, p)) { p.name = pname; p.enabled = true; }

    // --- off ---
    if (sub == "off") {
        p.proxy.clear();
        pt::store::save(p);
        std::cout << pname << ".proxy = (off)\n";
        return 0;
    }

    // --- on <preset> [profile] ---
    if (sub == "on") {
        const std::string preset_name = argc >= 4 ? argv[3] : "";
        const std::string prof = argc >= 5 ? argv[4] :
            (pname != preset_name ? pname : pt::store::current());
        pt::store::Profile p;
        if (!pt::store::load(prof, p)) { p.name = prof; p.enabled = true; }

        if (preset_name.empty()) {
            std::cerr << "usage: proxy on <preset_name> [profile]\n"; return 1;
        }
        const std::string url = host + "/api/ttys/proxy/presets?name="
                              + jesc(preset_name);
        const auto br = bearer_from();
        const pt::HttpResult pr = pt::get(url, br);
        if (pr.status != 200) {
            std::cerr << "preset '" << preset_name << "' not found\n";
            return 1;
        }
        auto extract = [&](const char* k) -> std::string {
            auto key = std::string("\"") + k + "\":";
            auto pos = pr.body.find(key);
            if (pos == std::string::npos) return "";
            pos += key.size();
            while (pos < pr.body.size() && pr.body[pos] == ' ') ++pos;
            if (pr.body[pos] == '"') {
                auto end = pr.body.find('"', ++pos);
                return end == std::string::npos ? "" :
                       pr.body.substr(pos, end - pos);
            }
            auto eol = pr.body.find_first_of(",}", pos);
            return eol == std::string::npos ? "" :
                   pr.body.substr(pos, eol - pos);
        };
        p.proxy = extract("type") + "://" + extract("host") + ":" +
                  extract("port");
        pt::store::save(p);
        std::cout << prof << ".proxy = " << p.proxy << "\n";
        return 0;
    }

    // --- explicit spec ---
    p.proxy = sub;
    pt::store::save(p);
    std::cout << pname << ".proxy = " << p.proxy << "\n";
    return 0;
}

int cmd_profile(int argc, char** argv) {
    const std::string sub = argc >= 3 ? argv[2] : "list";
    auto nm = [&](int idx) { return argc > idx ? argv[idx] : ""; };

    if (sub == "list" || sub.empty()) {
        std::cout << "profiles:\n";
        const std::string cur = pt::store::current();
        for (const auto& p : pt::store::all())
            std::cout << (p.name == cur ? "* " : "  ") << p.name
                      << (p.enabled ? "  [enabled]" : "  [disabled]")
                      << "  proxy=" << (p.proxy.empty() ? "(off)" : p.proxy)
                      << "  session=" << (p.session.empty() ? "(none)" : p.session)
                      << "\n";
        return 0;
    }
    if (sub == "create" || sub == "set") {
        const std::string name = nm(3);
        if (name.empty()) { std::cerr << "profile name required\n"; return 1; }
        pt::store::Profile p;
        if (!pt::store::load(name, p)) { p.name = name; p.enabled = true; }
        if (argc >= 5) p.proxy = argv[4];
        pt::store::save(p);
        std::cout << "profile " << name << " saved\n";
        return 0;
    }
    if (sub == "enable" || sub == "disable") {
        const std::string name = nm(3);
        if (!pt::store::set_enabled(name, sub == "enable")) {
            std::cerr << "cannot " << sub << " '" << name << "'\n";
            return 1;
        }
        std::cout << "profile " << name << " "
                  << (sub == "enable" ? "enabled" : "disabled") << "\n";
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
        if (!pt::store::remove(name)) {
            std::cerr << "cannot delete '" << name << "'\n";
            return 1;
        }
        std::cout << "profile " << name << " removed\n";
        return 0;
    }
    if (sub == "export" || sub == "import") {
        const std::string file = nm(3);
        if (file.empty()) {
            std::cerr << "usage: progterm-lite profile " << sub << " <file>\n";
            return 1;
        }
        const auto ps = pt::store::all();
        if (sub == "export") {
            if (ps.empty()) { std::cerr << "nothing to export\n"; return 1; }
            std::ofstream f(file, std::ios::trunc);
            f << "current " << pt::store::current() << "\n";
            for (const auto& p : ps)
                f << "profile " << p.name << "\nenabled "
                  << (p.enabled ? "true" : "false") << "\nproxy " << p.proxy
                  << "\nhost " << p.host << "\nsession " << p.session << "\n";
            f.close();
#ifdef __unix__
            chmod(file.c_str(), 0600);
#endif
            std::cout << "exported " << ps.size() << " profile(s)\n";
            return 0;
        }
        std::ifstream in(file);
        if (!in) { std::cerr << "cannot open " << file << "\n"; return 1; }
        std::string line, want_current;
        pt::store::Profile cur;
        bool have = false;
        int n = 0;
        auto commit = [&] {
            if (!have) return;
            pt::store::save(cur);
            ++n;
            have = false;
        };
        while (std::getline(in, line)) {
            if (line.rfind("profile ", 0) == 0) {
                commit();
                cur = pt::store::Profile();
                cur.name = line.substr(8);
                cur.enabled = true;
                have = true;
                continue;
            }
            if (line.rfind("current ", 0) == 0) { want_current = line.substr(8); continue; }
            if (!have || line.empty()) continue;
            const size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            const std::string k = line.substr(0, sp);
            const std::string v = line.substr(sp + 1);
            if (k == "enabled") cur.enabled = (v != "false");
            else if (k == "proxy")   cur.proxy  = v;
            else if (k == "host")    cur.host   = v;
            else if (k == "session") cur.session= v;
        }
        commit();
        if (!want_current.empty()) pt::store::set_current(want_current);
        std::cout << "imported " << n << " profile(s)\n";
        return 0;
    }
    std::cerr << "unknown profile action\n";
    return 1;
}

int cmd_logout(int argc, char** argv) {
    std::string pname = argc >= 3 ? argv[2] : pt::store::current();
    pt::store::Profile p;
    if (!pt::store::load(pname, p)) {
        std::cerr << "unknown profile '" << pname << "'\n";
        return 1;
    }
    p.session.clear();
    pt::store::save(p);
    std::cout << "logged out of profile " << pname << "\n";
    return 0;
}

int cmd_raw(const std::string& host, const std::string& bearer,
            int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: raw <path> [json-body]\n"; return 1; }
    std::string path = argv[2];
    if (!path.empty() && path[0] == '/') path.erase(0, 1);
    const std::string body = argc >= 4 ? argv[3] : "";
    const pt::HttpResult r = body.empty()
        ? pt::get(host + "/" + path, bearer)
        : pt::post(host + "/" + path, body, bearer);
    if (r.status == 0)
        return (!body.empty() && outbox_record(path, body)) ? 0 : 2;
    std::cout << r.body << "\n";
    return (r.status >= 200 && r.status < 300) ? 0 : 1;
}

}  // namespace

static int run_main(int argc, char** argv) {
    // A line beginning with '/' is ALWAYS a full-client REPL command.
    const bool slashed = argc >= 2 && argv[1][0] == '/';

    if (argc < 2) { usage(); return 1; }

    const Target tgt = resolve_target(argc, argv);
    const std::string host = !tgt.host.empty() ? tgt.host : host_from();
    const std::string bearer = bearer_from();
    outbox_flush(host, bearer);

    // Layout recovery: forgot-to-switch-layout words ("зкщчн ыефегы") map
    // back to US ("proxy status") and re-dispatch. Russian CHAT text never
    // matches a verb, so it falls through to the proxy essence untouched.
    if (!slashed && contains_cyrillic(argv[1])) {
        std::vector<std::string> t(argc);
        for (int i = 0; i < argc; ++i) t[i] = argv[i] ? argv[i] : "";
        t[1] = ru_to_us(t[1]);
        if (t[1] != argv[1]) {
            for (int i = 2; i < argc; ++i) t[i] = ru_to_us(t[i]);
            std::vector<char*> av(argc);
            for (int i = 0; i < argc; ++i) av[i] = t[i].data();
            return run_main(argc, av.data());
        }
    }

    if (!slashed) {
        if (argv[1] == std::string("help"))     { usage(); return 0; }
        if (argv[1] == std::string("register")) return cmd_register(host, bearer, argc, argv);
        if (argv[1] == std::string("session"))  return cmd_session(host, bearer, argc, argv);
        if (argv[1] == std::string("last"))     return cmd_last(host, bearer);
        if (argv[1] == std::string("sync"))     return cmd_sync(host, bearer, argc, argv);
        if (argv[1] == std::string("proxy"))    return cmd_proxy(argc, argv);
        if (argv[1] == std::string("profile"))  return cmd_profile(argc, argv);
        if (argv[1] == std::string("logout"))   return cmd_logout(argc, argv);
        if (argv[1] == std::string("raw"))      return cmd_raw(host, bearer, argc, argv);
        if (argv[1] == std::string("render")) {
            const std::string ses = sess_px(argc, argv).ses;
            if (ses.empty()) return 1;
            if (!fetch_frame(host, bearer, ses,
                             argc >= 4 ? argv[3] : "",
                             sess_px(argc, argv).px))
                outbox_record("api/ttys/render",
                              render_body(ses, argc >= 4 ? argv[3] : ""));
            return 0;
        }
        if (argv[1] == std::string("term")) {
            const std::string ses = sess_px(argc, argv).ses;
            if (ses.empty()) return 1;
            return term_loop(host, bearer, ses,
                             argc >= 4 ? argv[3] : "",
                             sess_px(argc, argv).px);
        }
    }

    // ---- THE PROXY ESSENCE ----
    // The whole tail is a LINE for the full client's REPL. The session comes
    // from the ACTIVE profile (or an explicit PROFILE NAME as first word) —
    // never from the line itself.
    std::string ses;
    {
        const std::string first = argc >= 3 ? argv[2] : "";
        pt::store::Profile p;
        if (!first.empty() && pt::store::load(first, p)
            && !p.session.empty())
            ses = p.session;
        else if (pt::store::load("", p) && !p.session.empty())
            ses = p.session;
    }
    if (ses.empty()) {
        std::cerr << "no session yet:\n"
                     "  progterm-lite register <homeserver> <user> <pass> [profile]\n"
                     "  (or run 'progterm-lite last' against a live relay)\n";
        return 1;
    }

    std::string line = argv[1];
    for (int i = 2; i < argc; ++i) line += std::string(" ") + argv[i];
    const std::string ibody = with_proxy(
        "{\"session\":\"" + jesc(ses) +
        "\",\"input\":\"" + jesc(line) + "\"}",
        sess_px(argc, argv).px);

    const pt::HttpResult ir =
        pt::post(host + "/api/ttys/input", ibody, bearer);
    if (ir.status == 0 && outbox_record("api/ttys/input", ibody))
        return 0;

    fetch_frame(host, bearer, ses, "", sess_px(argc, argv).px);
    return ir.status >= 200 && ir.status < 300 ? 0 : 1;
}

int main(int argc, char** argv) { return run_main(argc, argv); }
