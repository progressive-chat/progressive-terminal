#pragma once
#include <string>

namespace pt {

// Interactive TUI loop. Compiled in only when PROGTERM_TUI is defined.
// Clears the screen, shows the static frame, reads a line of input, posts it
// to the server, and repeats. Re-renders on SIGWINCH (terminal resize).
int run_tui(const std::string& host,
            const std::string& session,
            const std::string& room);

}  // namespace pt
