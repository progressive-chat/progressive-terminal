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
// Returns the HTTP status and response body.
HttpResult http_post_json(const std::string& url, const std::string& body);

}  // namespace pt
