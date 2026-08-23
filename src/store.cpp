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
#endif

namespace pt { namespace store {

namespace {
std::string config_dir() {
#ifdef __unix__
    std::string home = getenv("HOME") ? getenv("HOME") : ".";
    return home + "/.config/progressive-terminal";
#else
    return ".progressive-terminal";
#endif
}

std::string session_path() {
    return config_dir() + "/session";
}
}  // namespace

bool save_session(const std::string& host, const std::string& session) {
    if (session.empty()) return false;
#ifdef __unix__
    mkdir(getenv("HOME") ? getenv("HOME") : ".", 0700);
    mkdir((getenv("HOME") ? std::string(getenv("HOME")) : std::string(".") +
           "/.config").c_str(), 0700);
    mkdir(config_dir().c_str(), 0700);
#endif
    std::ofstream f(session_path(), std::ios::trunc);
    if (!f) return false;
    f << "{\"host\":" << pt::json::str(host)
      << ",\"session\":" << pt::json::str(session) << "}\n";
    return static_cast<bool>(f);
}

bool load_session(std::string& host, std::string& session) {
    std::ifstream f(session_path());
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string content = ss.str();
    bool ok = pt::json::get_string(content, "session", session);
    pt::json::get_string(content, "host", host);  // optional
    return ok && !session.empty();
}

bool clear_session() {
    return std::remove(session_path().c_str()) == 0;
}

}}  // namespace pt::store
