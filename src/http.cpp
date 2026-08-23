#include "http.hpp"
#include <curl/curl.h>

namespace pt {

namespace {

size_t write_cb(void* ptr, size_t size, size_t nmemb, std::string* user) {
    const size_t r = size * nmemb;
    user->append(static_cast<char*>(ptr), r);
    return r;
}

// One request path for GET and POST: an empty `body` means GET.
HttpResult send(const std::string& url, const std::string& body,
                const std::string& bearer) {
    HttpResult res;
    CURL* c = curl_easy_init();
    if (!c) return res;

    struct curl_slist* hdr = nullptr;
    if (!body.empty())
        hdr = curl_slist_append(hdr, "Content-Type: application/json");
    if (!bearer.empty())
        hdr = curl_slist_append(hdr, ("Authorization: Bearer " + bearer).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    if (hdr) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res.body);
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }

    res.code = curl_easy_perform(c);
    if (res.code == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.http_status);

    if (hdr) curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return res;
}

}  // namespace

HttpResult http_post_json(const std::string& url, const std::string& body,
                          const std::string& bearer) {
    return send(url, body, bearer);
}

HttpResult http_get_json(const std::string& url, const std::string& bearer) {
    return send(url, "", bearer);
}

}  // namespace pt
