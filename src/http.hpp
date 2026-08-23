#pragma once
#include <string>
#include <curl/curl.h>

namespace pt {

struct HttpResult {
    CURLcode code = CURLE_OK;
    long http_status = 0;
    std::string body;
    bool ok() const { return code == CURLE_OK && http_status >= 200 && http_status < 300; }
};

// POST `body` (JSON) to `url` with Content-Type: application/json.
// When `bearer` is non-empty an "Authorization: Bearer <bearer>" header is
// sent (relay token auth). Returns the HTTP status and response body.
HttpResult http_post_json(const std::string& url, const std::string& body,
                          const std::string& bearer = "");

// Same as http_post_json but asks the relay for a plain-text body
// ("Accept: text/plain") — the dumb terminal mode needs no JSON parser.
HttpResult http_post_plain(const std::string& url, const std::string& body,
                           const std::string& bearer = "");

// GET `url` (used to query relay status). Returns the HTTP status and body.
HttpResult http_get_json(const std::string& url, const std::string& bearer = "");

}  // namespace pt
