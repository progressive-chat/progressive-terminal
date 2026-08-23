#include "store.hpp"
#include "json.hpp"
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
    std::string home = getenv("HOME") ? getenv("HOME") : ".";
    mkdir(home.c_str(), 0700);
    mkdir((home + "/.config").c_str(), 0700);
    mkdir(base_dir().c_str(), 0700);
    mkdir(profiles_dir().c_str(), 0700);
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

bool any_enabled() {
    for (auto& p : list_profiles()) if (p.enabled) return true;
    return false;
}

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
    const std::string body = "{"
        "\"enabled\":" + std::string(p.enabled ? "true" : "false") +
        ",\"proxy\":"    + pt::json::str(p.proxy) +
        ",\"host\":"     + pt::json::str(p.host) +
        ",\"session\":"  + pt::json::str(p.session) + "}";
    write_file(profile_path(p.name), body);
    ensure_at_least_one_enabled(p.name);
    // Keep current pointing at an enabled profile.
    Profile cur;
    if (current_name().empty() || !load_profile(current_name(), cur)) {
        Profile en;
        if (load_profile("", en)) set_current(en.name);
    }
    return true;
}

bool load_profile(std::string name, Profile& out) {
    if (name.empty()) name = current_name();
    if (name.empty()) {
        // pick first enabled profile
        for (auto& p : list_profiles()) if (p.enabled) { name = p.name; break; }
    }
    if (name.empty()) return false;
    const std::string content = read_file(profile_path(name));
    if (content.empty()) return false;
    out.name = name;
    std::string en;
    if (pt::json::get_string(content, "enabled", en))
        out.enabled = (en == "true");
    pt::json::get_string(content, "proxy", out.proxy);
    pt::json::get_string(content, "host", out.host);
    pt::json::get_string(content, "session", out.session);
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

bool set_enabled(const std::string& name, bool on) {
    Profile p;
    if (!load_profile(name, p)) return false;
    if (!on && p.enabled) {
        // count enabled others
        int n = 0;
        for (auto& q : list_profiles()) if (q.enabled && q.name != name) n++;
        if (n == 0) return false;  // refuse: would leave zero enabled
    }
    p.enabled = on;
    save_profile(p);
    return true;
}

bool remove_profile(std::string name) {
    if (name.empty()) name = current_name();
    if (name.empty()) return false;
    Profile p;
    if (!load_profile(name, p)) return false;
    if (p.enabled) {
        int n = 0;
        for (auto& q : list_profiles()) if (q.enabled && q.name != name) n++;
        if (n == 0) return false;  // refuse: last enabled
    }
    std::remove(profile_path(name).c_str());
    if (current_name() == name) {
        for (auto& q : list_profiles()) if (q.enabled) { set_current(q.name); break; }
    }
    return true;
}

bool clear_all() {
    for (auto& p : list_profiles()) std::remove(profile_path(p.name).c_str());
    std::remove(current_file().c_str());
#ifdef __unix__
    rmdir(profiles_dir().c_str());
    rmdir(base_dir().c_str());
#endif
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
