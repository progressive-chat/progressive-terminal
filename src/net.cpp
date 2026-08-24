#include "net.hpp"
#include <curl/curl.h>

namespace pt {

namespace {
size_t sink(char* p, size_t sz, size_t n, std::string* out) {
    out->append(p, sz * n);
    return sz * n;
}
}  // namespace

// One request path: empty body = GET; bearer/accept add headers when set.
HttpResult http(const std::string& url, const std::string& body,
                const std::string& bearer, const char* accept) {
    HttpResult r;
    CURL* c = curl_easy_init();
    if (!c) return r;

    curl_slist* hdr = nullptr;
    if (!body.empty()) hdr = curl_slist_append(hdr, "Content-Type: application/json");
    if (!bearer.empty()) hdr = curl_slist_append(hdr, ("Authorization: Bearer " + bearer).c_str());
    if (accept && *accept) hdr = curl_slist_append(hdr, ("Accept: " + std::string(accept)).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    if (hdr) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    if (curl_easy_perform(c) == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    else
        r.body.clear();
    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return r;
}

std::string discover_relay(int base, int range) {
    for (int d = 0; d <= range; ++d)
        for (int s : {1, -1}) {
            if (!d && s < 0) continue;
            const std::string u =
                "http://127.0.0.1:" + std::to_string(base + s * d);
            // A real relay answers /sync (even for a bogus session) with our
            // markers; token-protected ones say "unauthorized".
            const auto r = post(u + "/api/ttys/sync", "{\"session\":\"?\"}");
            if ((r.status == 200 || r.status == 401 || r.status == 404) &&
                !r.body.empty())
                return u;
        }
    return "";
}

}  // namespace pt
