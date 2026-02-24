#ifndef CHATTERBOX_SERVER_SIGNAL_HANDLER_HPP
#define CHATTERBOX_SERVER_SIGNAL_HANDLER_HPP

namespace chatterbox {
namespace server {

// Install SIGTERM / SIGINT handlers for graceful shutdown.
// Also suppresses SIGPIPE so broken client pipes are handled cleanly.
void install_signal_handlers();

// Returns true once SIGTERM or SIGINT has been received.
bool shutdown_requested();

// Returns the last signal number that triggered a shutdown, or 0.
int last_signal();

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_SIGNAL_HANDLER_HPP
