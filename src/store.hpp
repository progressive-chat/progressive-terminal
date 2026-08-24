#pragma once
#include <string>
#include <vector>

namespace pt { namespace store {

struct Profile {
    std::string name, proxy, host, session;
    bool enabled = true;
};

bool save(Profile& p);                       // upsert + invariant + current fix
bool load(std::string name, Profile& out);   // "" = current (или первый включённый)
std::string current();
bool set_current(const std::string& name);   // только включённый
std::vector<Profile> all();
bool set_enabled(const std::string& name, bool on);  // ≥1 остаётся
bool remove(const std::string& name);                // ≥1 остаётся
void ensure_default();

}}  // namespace pt::store
