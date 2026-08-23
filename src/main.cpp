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

#include <iostream>
#include <string>
#include <cstdlib>

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

void usage() {
    std::cerr << "usage: progterm <path> [json-body]\n"
                 "env:   PROGTERM_HOST  PROGTERM_TOKEN\n"
                 "example: progterm api/ttys/proxy "
              << "'{\"action\":\"on\",\"preset\":\"tor\"}'\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string path = argv[1];
    if (!path.empty() && path[0] == '/') path.erase(0, 1);
    const std::string body = argc >= 3 ? argv[2] : "";
    const std::string url = host_from() + "/" + path;

    const pt::HttpResult r = body.empty()
        ? pt::http_get_json(url, bearer_from())
        : pt::http_post_json(url, body, bearer_from());

    if (r.http_status == 0) {
        std::cerr << "progterm: cannot reach relay (curl "
                  << static_cast<long>(r.code) << ")\n";
        return 2;
    }
    std::cout << r.body << "\n";
    return (r.http_status >= 200 && r.http_status < 300) ? 0 : 1;
}
