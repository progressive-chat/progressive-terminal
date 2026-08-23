#include "store.hpp"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>

#ifdef __unix__
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#endif

namespace pt { namespace store {

// Profile files are a one-time cache, so the format is the simplest thing
// that survives: one "key=value" line per field (enabled, proxy, host,
// session). Values never contain newlines. A file that can't be parsed is
// treated as empty — deleting it would be an equally valid "migration".

namespace {
std::string base_dir() {
#ifdef __unix__
    std::string home = getenv("HOME") ? getenv("HOME") : ".";
    return home + "/.config/progressive-terminal";
#else
    return ".progressive-terminal";
#endif
}
std::string profiles_dir() { return base_dir() + "/profiles"; }
std::string current_file() { return base_dir() + "/current"; }
std::string profile_path(const std::string& name) {
    return profiles_dir() + "/" + name;
}

void ensure_dirs() {
#ifdef __unix__
    std::string d = getenv("HOME") ? getenv("HOME") : ".";
    for (std::string p : {d + "/.config", base_dir(), profiles_dir()}) {
        mkdir(p.c_str(), 0700);
        d = p;
    }
#endif
}

std::string read_file(const std::string& p) {
    std::ifstream f(p);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}
void write_file(const std::string& p, const std::string& s) {
    std::ofstream f(p, std::ios::trunc);
    if (f) f << s;
}

// Value of "key=" in a key=value document, or "" when absent.
std::string kv_get(const std::string& content, const std::string& key) {
    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find('\n', pos);
        if (eol == std::string::npos) eol = content.size();
        if (content.compare(pos, key.size() + 1, key + "=") == 0)
            return content.substr(pos + key.size() + 1, eol - pos - key.size() - 1);
        pos = eol + 1;
    }
    return "";
}

void parse_profile(const std::string& content, Profile& p) {
    p.enabled = kv_get(content, "enabled") != "false";
    p.proxy   = kv_get(content, "proxy");
    p.host    = kv_get(content, "host");
    p.session = kv_get(content, "session");
}

bool any_enabled() {
    for (auto& p : list_profiles()) if (p.enabled) return true;
    return false;
}

// Invariant: at least one enabled profile must always exist. After any save,
// re-enable `preferred`, else the first profile found.
void ensure_at_least_one_enabled(const std::string& preferred) {
    if (any_enabled()) return;
    Profile p;
    if (load_profile(preferred, p)) { p.enabled = true; save_profile(p); }
    else if (!list_profiles().empty()) {
        p = list_profiles().front(); p.enabled = true; save_profile(p);
    }
}
}  // namespace

bool save_profile(const Profile& p) {
    if (p.name.empty()) return false;
    ensure_dirs();
    write_file(profile_path(p.name),
        "enabled=" + std::string(p.enabled ? "true" : "false") + "\n" +
        "proxy="   + p.proxy  + "\n" +
        "host="    + p.host   + "\n" +
        "session=" + p.session + "\n");
    ensure_at_least_one_enabled(p.name);
    // Keep current pointing at an existing, enabled profile.
    Profile cur;
    if (current_name().empty() || !load_profile(current_name(), cur)) {
        Profile en;
        if (load_profile("", en)) set_current(en.name);
    }
    return true;
}

bool load_profile(std::string name, Profile& out) {
    if (name.empty()) name = current_name();
    if (name.empty())
        for (auto& p : list_profiles()) if (p.enabled) { name = p.name; break; }
    if (name.empty()) return false;
    const std::string content = read_file(profile_path(name));
    if (content.empty()) return false;
    out.name = name;
    parse_profile(content, out);
    return true;
}

std::string current_name() {
    std::string s = read_file(current_file());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

bool set_current(const std::string& name) {
    Profile p;
    if (!load_profile(name, p)) return false;
    if (!p.enabled) return false;
    ensure_dirs();
    write_file(current_file(), name);
    return true;
}

std::vector<Profile> list_profiles() {
    std::vector<Profile> out;
#ifdef __unix__
    DIR* d = opendir(profiles_dir().c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        Profile p;
        if (load_profile(n, p)) out.push_back(p);
    }
    closedir(d);
#endif
    return out;
}

// Number of OTHER enabled profiles (used by disable/delete guards).
int others_enabled(const std::string& name) {
    int n = 0;
    for (auto& q : list_profiles()) if (q.enabled && q.name != name) n++;
    return n;
}

bool set_enabled(const std::string& name, bool on) {
    Profile p;
    if (!load_profile(name, p)) return false;
    if (!on && p.enabled && others_enabled(name) == 0)
        return false;  // refuse: would leave zero enabled
    p.enabled = on;
    save_profile(p);
    return true;
}

bool remove_profile(std::string name) {
    if (name.empty()) return false;
    Profile p;
    if (!load_profile(name, p)) return false;
    if (p.enabled && others_enabled(name) == 0)
        return false;  // refuse: last enabled
    std::remove(profile_path(name).c_str());
    if (current_name() == name) {
        for (auto& q : list_profiles()) if (q.enabled) { set_current(q.name); break; }
    }
    return true;
}

void ensure_default_profile() {
    if (!list_profiles().empty()) return;
    Profile p;
    p.name = "default";
    p.enabled = true;
    save_profile(p);
}

}}  // namespace pt::store
