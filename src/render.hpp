#pragma once
#include <string>
#include "term_size.hpp"

namespace pt {

// Request one rendered frame from a `progressive-cli serve --ttys` server.
// Detects the terminal size (unless cols/rows are forced) and POSTs it as
// term:{cols,rows}. When `static_only` is set, the server returns a single
// non-interactive ASCII snapshot. Returns the raw frame text (newlines
// already unescaped), or an error string prefixed with "error:".
std::string request_frame(const std::string& host,
                          const std::string& session,
                          const std::string& room,
                          bool static_only,
                          int cols = 0, int rows = 0);

// Print usage for the `render` subcommand.
void usage_render();

}  // namespace pt
