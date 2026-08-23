#pragma once
#include <string>
#include <vector>

namespace pt { namespace store {

// A profile is a container for connection settings and an OPTIONAL account
// (session). It can exist with no account (just proxy/host config). At least
// one profile is always enabled.
struct Profile {
    std::string name;
    bool enabled = true;
    std::string proxy;    // socks5://[u:p@]h:p | http://h:p | off | "" (server default)
    std::string host;
    std::string session;
    bool has_account() const { return !session.empty(); }
};

// Upsert profile `p.name`. Enforces the "at least one enabled" invariant.
bool save_profile(const Profile& p);

// Load profile `name`; when empty, load the current (enabled) profile.
// Returns false if none found.
bool load_profile(std::string name, Profile& out);

std::string current_name();
bool set_current(const std::string& name);   // must be enabled

std::vector<Profile> list_profiles();

// Enable/disable a profile. Refuses to disable the last enabled one.
bool set_enabled(const std::string& name, bool on);

// Remove a profile. Refuses if it is the only enabled one.
bool remove_profile(std::string name);

bool clear_all();

// Create a default enabled profile ("default") if no profiles exist.
void ensure_default_profile();

}}  // namespace pt::store
