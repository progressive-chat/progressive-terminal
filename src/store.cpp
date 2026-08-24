#include "store.hpp"
#include <fstream>
#include <sstream>

#ifdef __unix__
#include <sys/stat.h>
#include <dirent.h>
#endif

namespace pt { namespace store {

namespace {
std::string base() {
    const char* h = getenv("HOME");
    return (h ? h : ".") + std::string("/.config/progterm-lite");
}
std::string file(const std::string& n)   { return base() + "/profiles/" + n; }
std::string cur_file()                   { return base() + "/current"; }

void mkdirs() {
#ifdef __unix__
    const char* h = getenv("HOME");
    std::string d = h ? h : ".";
    mkdir(d.c_str(), 0700);
    mkdir((d + "/.config").c_str(), 0700);
    mkdir(base().c_str(), 0700);
    mkdir((base() + "/profiles").c_str(), 0700);
#endif
}

std::string read(const std::string& p) {
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
void write(const std::string& p, const std::string& s) {
    std::ofstream f(p, std::ios::trunc);
    if (f) f << s;
}

std::string kv(const std::string& doc, const std::string& key) {
    for (size_t p = 0; p < doc.size();) {
        size_t e = doc.find('\n', p);
        if (e == std::string::npos) e = doc.size();
        if (doc.compare(p, key.size() + 1, key + "=") == 0)
            return doc.substr(p + key.size() + 1, e - p - key.size() - 1);
        p = e + 1;
    }
    return "";
}

Profile parse(const std::string& doc, const std::string& name) {
    Profile p;
    p.name    = name;
    p.enabled = kv(doc, "enabled") != "false";
    p.proxy   = kv(doc, "proxy");
    p.host    = kv(doc, "host");
    p.session = kv(doc, "session");
    return p;
}

bool any_enabled() {
    for (auto& p : all())
        if (p.enabled) return true;
    return false;
}
}  // namespace

bool save(Profile& p) {
    if (p.name.empty()) return false;
    mkdirs();
    write(file(p.name),
          "enabled=" + std::string(p.enabled ? "true" : "false") +
          "\nproxy="   + p.proxy +
          "\nhost="    + p.host +
          "\nsession=" + p.session + "\n");

    // Invariant: ≥1 enabled. Keep `current` pointing at an enabled profile.
    if (!any_enabled()) { p.enabled = true; write(file(p.name),
        "enabled=true\nproxy="+p.proxy+"\nhost="+p.host+"\nsession="+p.session+"\n"); }
    Profile c;
    if (current().empty() || !load(current(), c))
        if (load("", c)) set_current(c.name);
    return true;
}

bool load(std::string name, Profile& out) {
    if (name.empty()) name = current();
    if (name.empty())
        for (auto& p : all())
            if (p.enabled) { name = p.name; break; }
    if (name.empty()) return false;

    const std::string doc = read(file(name));
    if (doc.empty()) return false;
    out = parse(doc, name);
    return true;
}

std::vector<Profile> all() {
    std::vector<Profile> v;
#ifdef __unix__
    DIR* d = opendir((base() + "/profiles").c_str());
    if (!d) return v;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == ".." || n[0] == '.') continue;
        Profile p;
        if (load(n, p)) v.push_back(p);
    }
    closedir(d);
#endif
    return v;
}

std::string current() {
    std::string s = read(cur_file());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

bool set_current(const std::string& name) {
    Profile p;
    if (!load(name, p) || !p.enabled) return false;
    mkdirs();
    write(cur_file(), name);
    return true;
}

bool set_enabled(const std::string& name, bool on) {
    Profile p;
    if (!load(name, p)) return false;
    if (!on && p.enabled) {
        int others = 0;
        for (auto& q : all())
            if (q.enabled && q.name != name) ++others;
        if (!others) return false;              // refuse: last enabled
    }
    p.enabled = on;
    return save(p);
}

bool remove(const std::string& name) {
    Profile p;
    if (!load(name, p)) return false;
    if (p.enabled) {
        int others = 0;
        for (auto& q : all())
            if (q.enabled && q.name != name) ++others;
        if (!others) return false;              // refuse: last enabled
    }
    std::remove(file(name).c_str());
    if (current() == name)
        for (auto& q : all())
            if (q.enabled) { set_current(q.name); break; }
    return true;
}

void ensure_default() {
    if (!all().empty()) return;
    Profile p;
    p.name = "default";
    save(p);
}

}}  // namespace pt::store
