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
std::string accounts_dir() { return base_dir() + "/accounts"; }
std::string current_file() { return base_dir() + "/current"; }
std::string account_path(const std::string& name) {
    return accounts_dir() + "/" + name;
}

void ensure_dirs() {
#ifdef __unix__
    std::string home = getenv("HOME") ? getenv("HOME") : ".";
    mkdir(home.c_str(), 0700);
    mkdir((home + "/.config").c_str(), 0700);
    mkdir(base_dir().c_str(), 0700);
    mkdir(accounts_dir().c_str(), 0700);
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
}  // namespace

bool save_account(const Account& a) {
    if (a.name.empty()) return false;
    ensure_dirs();
    const std::string body = "{"
        "\"host\":"    + pt::json::str(a.host) +
        ",\"session\":" + pt::json::str(a.session) +
        ",\"proxy\":"   + pt::json::str(a.proxy) + "}";
    write_file(account_path(a.name), body);
    set_current(a.name);
    return true;
}

bool load_account(std::string name, Account& out) {
    if (name.empty()) name = current_name();
    if (name.empty()) return false;
    const std::string content = read_file(account_path(name));
    if (content.empty()) return false;
    out.name = name;
    pt::json::get_string(content, "host", out.host);
    pt::json::get_string(content, "session", out.session);
    pt::json::get_string(content, "proxy", out.proxy);
    return !out.session.empty();
}

std::string current_name() {
    std::string s = read_file(current_file());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

bool set_current(const std::string& name) {
    ensure_dirs();
    write_file(current_file(), name);
    return true;
}

std::vector<Account> list_accounts() {
    std::vector<Account> out;
#ifdef __unix__
    DIR* d = opendir(accounts_dir().c_str());
    if (!d) return out;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        Account a;
        if (load_account(n, a)) out.push_back(a);
    }
    closedir(d);
#endif
    return out;
}

bool remove_account(std::string name) {
    if (name.empty()) name = current_name();
    if (name.empty()) return false;
    const std::string p = account_path(name);
    bool removed = (std::remove(p.c_str()) == 0);
    if (removed && current_name() == name) {
        // pick another account as current, else clear
        auto all = list_accounts();
        set_current(all.empty() ? "" : all.front().name);
    }
    return removed;
}

bool clear_all() {
    bool ok = true;
#ifdef __unix__
    for (auto& a : list_accounts()) std::remove(account_path(a.name).c_str());
    ok = (std::remove(current_file().c_str()) == 0) || ok;
    rmdir(accounts_dir().c_str());
    rmdir(base_dir().c_str());
#endif
    return ok;
}

}}  // namespace pt::store
