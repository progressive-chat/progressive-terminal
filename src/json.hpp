#pragma once
#include <string>

namespace pt { namespace json {

// Quote + escape a string as a JSON string literal (returns the quoted form).
std::string str(const std::string& s);

// Escape a raw string for inclusion inside a JSON value (no surrounding quotes).
std::string escape(const std::string& s);

// Extract the first "key":"value" string occurrence. Returns false if absent.
bool get_string(std::string_view body, std::string_view key, std::string& out);

// Extract the first "key":<int> occurrence. Returns false if absent.
bool get_int(std::string_view body, std::string_view key, long& out);

// Unescape a JSON string body (\n \t \" \\ \/) into a raw string.
std::string unescape(std::string_view s);

}}  // namespace pt::json
