#pragma once
#include <string>

namespace pt { namespace proto {

// The wire contract of `progressive-cli serve --ttys`, in ONE place.
// Every request body the client can send is built here; nothing else
// concatenates protocol JSON.

std::string registerBody(const std::string& homeserver,
                         const std::string& username,
                         const std::string& password,
                         const std::string& regToken,
                         const std::string& proxy);

std::string sessionBody(const std::string& homeserver,
                        const std::string& user,
                        const std::string& token,
                        const std::string& device,
                        const std::string& proxy);

std::string inputBody(const std::string& session, const std::string& text);
std::string syncBody(const std::string& session);

std::string renderBody(const std::string& session, int cols, int rows,
                       const std::string& room, bool staticOnly);

std::string proxyOnBody(const std::string& preset);
std::string proxyOffBody();

}}  // namespace pt::proto
