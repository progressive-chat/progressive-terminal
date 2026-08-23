#pragma once
#include <string>

namespace pt {

struct TermSize { int cols = 80; int rows = 24; };

// Detect the current terminal size. Uses TIOCGWINSZ when available, then
// falls back to the COLUMNS/LINES environment variables, then 80x24.
TermSize detect_terminal_size();

}  // namespace pt
