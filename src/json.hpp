#pragma once
#include <string>

namespace pt { namespace json {

// Quote + escape a string as a JSON string literal (returns the quoted form).
std::string str(const std::string& s);

// Extract the first "key":"value" string occurrence. Returns false if absent.
bool get_string(std::string_view body, std::string_view key, std::string& out);

}}  // namespace pt::json
