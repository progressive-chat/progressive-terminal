#pragma once
#include <string>

namespace pt { namespace store {

// Persist the active (host, session) pair so subsequent invocations don't
// need --session / --host on every call. Stored as a tiny JSON file under
// $HOME/.config/progressive-terminal/session. This is the ONLY local state
// progressive-terminal keeps — no message DB, no Matrix data.
bool save_session(const std::string& host, const std::string& session);

// Load a previously saved (host, session). Returns false if absent or
// unreadable. Either output may be left untouched when not present.
bool load_session(std::string& host, std::string& session);

// Remove the cached session file. Returns true if something was removed.
bool clear_session();

}}  // namespace pt::store
