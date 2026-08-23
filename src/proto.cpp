#include "proto.hpp"
#include "json.hpp"

namespace pt { namespace proto {

namespace {
std::string j(const std::string& s) { return json::str(s); }
}

std::string registerBody(const std::string& homeserver,
                         const std::string& username,
                         const std::string& password,
                         const std::string& regToken,
                         const std::string& proxy) {
    return "{\"homeserver\":" + j(homeserver) +
           ",\"username\":"   + j(username) +
           ",\"password\":"   + j(password) +
           ",\"reg_token\":"  + j(regToken) +
           ",\"proxy\":"      + j(proxy) + "}";
}

std::string sessionBody(const std::string& homeserver,
                        const std::string& user,
                        const std::string& token,
                        const std::string& device,
                        const std::string& proxy) {
    return "{\"account\":{\"homeserver\":" + j(homeserver) +
           ",\"user_id\":"     + j(user) +
           ",\"access_token\":" + j(token) +
           ",\"device_id\":"   + j(device) +
           ",\"proxy\":"       + j(proxy) + "}}";
}

std::string inputBody(const std::string& session, const std::string& text) {
    return "{\"session\":" + j(session) + ",\"input\":" + j(text) + "}";
}

std::string syncBody(const std::string& session) {
    return "{\"session\":" + j(session) + "}";
}

std::string renderBody(const std::string& session, int cols, int rows,
                       const std::string& room, bool staticOnly) {
    std::string b = "{\"session\":" + j(session);
    b += ",\"term\":{\"cols\":" + std::to_string(cols) +
         ",\"rows\":" + std::to_string(rows) + "}";
    if (!room.empty()) b += ",\"room\":" + j(room);
    if (staticOnly)    b += ",\"view\":\"static\"";
    return b + "}";
}

std::string proxyOnBody(const std::string& preset) {
    return "{\"action\":\"on\",\"preset\":" + j(preset) + "}";
}

std::string proxyOffBody() {
    return "{\"action\":\"off\"}";
}

}}  // namespace pt::proto
