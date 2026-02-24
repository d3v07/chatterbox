#include <chatterbox/server/server.hpp>
#include <csignal>
#include <atomic>
#include <iostream>

// Global shutdown flag set by signal handlers.
// The main application loop polls this flag and calls Server::stop().
namespace chatterbox {
namespace server {

namespace {
    std::atomic<bool> g_shutdown_requested{false};
    std::atomic<int>  g_signal_received{0};
}

static void handle_signal(int sig) {
    g_signal_received.store(sig);
    g_shutdown_requested.store(true);
}

// Install SIGTERM and SIGINT handlers so the server can shut down gracefully.
// Call this once before starting the server event loop.
void install_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa, nullptr) != 0) {
        std::perror("sigaction(SIGTERM)");
    }
    if (sigaction(SIGINT, &sa, nullptr) != 0) {
        std::perror("sigaction(SIGINT)");
    }

    // Ignore SIGPIPE so broken pipes don't crash the server.
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    if (sigaction(SIGPIPE, &ignore, nullptr) != 0) {
        std::perror("sigaction(SIGPIPE)");
    }
}

bool shutdown_requested() {
    return g_shutdown_requested.load(std::memory_order_relaxed);
}

int last_signal() {
    return g_signal_received.load(std::memory_order_relaxed);
}

} // namespace server
} // namespace chatterbox
