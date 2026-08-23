#include "term_size.hpp"
#include <cstdlib>

#ifdef __unix__
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace pt {

TermSize detect_terminal_size() {
    TermSize s;
#ifdef TIOCGWINSZ
    struct winsize w {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        s.cols = static_cast<int>(w.ws_col);
        s.rows = static_cast<int>(w.ws_row);
        return s;
    }
#endif
    if (const char* c = std::getenv("COLUMNS"))
        if (const int v = std::atoi(c)) s.cols = v;
    if (const char* r = std::getenv("LINES"))
        if (const int v = std::atoi(r)) s.rows = v;
    return s;
}

}  // namespace pt
