#pragma once
#include <string>
#include <vector>

namespace pt { namespace store {

// Multi-account local cache. Each account is a separate JSON file under
//   $HOME/.config/progressive-terminal/accounts/<name>
// and the "active" one is tracked in .../current. This is the ONLY local
// state progressive-terminal keeps — no message DB, no Matrix data.
struct Account {
    std::string name;
    std::string host;
    std::string session;
    std::string proxy;   // socks5://.. | http://.. | off | "" (server default)
    bool valid() const { return !session.empty(); }
};

// Upsert account `a.name` (creating the file) and make it the current one.
bool save_account(const Account& a);

// Load account `name`; when `name` is empty, load the current account.
// Returns false if not found.
bool load_account(std::string name, Account& out);

std::string current_name();
bool set_current(const std::string& name);

// All known accounts (may include ones without a session yet).
std::vector<Account> list_accounts();

// Remove account `name` (or the current one when empty). Returns true if a
// file was removed.
bool remove_account(std::string name);

// Wipe the whole cache directory.
bool clear_all();

}}  // namespace pt::store
