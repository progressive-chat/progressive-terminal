#include "http.hpp"
#include <curl/curl.h>

namespace pt {

namespace {
size_t write_cb(void* ptr, size_t size, size_t nmemb, std::string* user) {
    const size_t r = size * nmemb;
    user->append(static_cast<char*>(ptr), r);
    return r;
}
}  // namespace

HttpResult http_post_json(const std::string& url, const std::string& body) {
    HttpResult res;
    CURL* c = curl_easy_init();
    if (!c) return res;

    struct curl_slist* hdr = nullptr;
    hdr = curl_slist_append(hdr, "Content-Type: application/json");

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &res.body);

    res.code = curl_easy_perform(c);
    if (res.code == CURLE_OK)
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.http_status);

    curl_slist_free_all(hdr);
    curl_easy_cleanup(c);
    return res;
}

}  // namespace pt
