#include "tui.hpp"
#include "http.hpp"
#include "json.hpp"
#include "render.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <cstring>

#ifdef __unix__
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace {

volatile std::sig_atomic_t g_resize = 0;
void on_winch(int) { g_resize = 1; }

}  // namespace

namespace pt {

int run_tui(const std::string& host,
            const std::string& session,
            const std::string& room,
            const std::string& bearer) {
#ifdef __unix__
    struct sigaction sa {};
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, nullptr);
#endif

    std::string line;
    while (true) {
        if (g_resize) g_resize = 0;

        const std::string frame = request_frame(host, session, room, true);
        // Clear screen + home cursor, then print the frame.
        std::cout << "\033[2J\033[H" << frame << "\n";
        std::cout << "progressive-terminal> ";
        std::cout.flush();

        if (!std::getline(std::cin, line)) break;  // EOF
        if (line == ":q" || line == ":quit" || line == "/quit") break;
        if (line.empty()) continue;

        const std::string body = "{"
            "\"session\":" + json::str(session) +
            ",\"input\":" + json::str(line) + "}";
        http_post_json(host + "/api/ttys/input", body, bearer);
    }
    return 0;
}

}  // namespace pt
