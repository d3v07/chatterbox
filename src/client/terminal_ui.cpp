#include <chatterbox/client/terminal_ui.hpp>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>

namespace chatterbox {
namespace client {

// Static instance for signal handler
TerminalUI* TerminalUI::instance_ = nullptr;

TerminalUI::TerminalUI(const UIConfig& config)
    : config_(config)
    , width_(80)
    , height_(24)
    , raw_mode_enabled_(false)
    , scroll_offset_(0)
    , cursor_position_(0)
    , history_index_(0)
{
    instance_ = this;
}

TerminalUI::~TerminalUI() {
    stop();
    instance_ = nullptr;
}

void TerminalUI::start() {
    if (running_.exchange(true)) {
        return;
    }

    update_terminal_size();
    enable_raw_mode();

    // Set up resize signal handler
    signal(SIGWINCH, handle_resize_signal);

    clear_screen();
    draw_screen();
}

void TerminalUI::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    disable_raw_mode();

    // Restore signal handler
    signal(SIGWINCH, SIG_DFL);

    // Show cursor and reset
    std::cout << colors::SHOW_CURSOR << colors::RESET << std::endl;
}

void TerminalUI::enable_raw_mode() {
    if (raw_mode_enabled_) return;

    tcgetattr(STDIN_FILENO, &original_termios_);

    struct termios raw = original_termios_;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // 100ms timeout

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled_ = true;
}

void TerminalUI::disable_raw_mode() {
    if (!raw_mode_enabled_) return;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios_);
    raw_mode_enabled_ = false;
}

void TerminalUI::update_terminal_size() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        width_ = ws.ws_col;
        height_ = ws.ws_row;
    }
}

void TerminalUI::handle_resize_signal(int sig) {
    (void)sig;
    if (instance_) {
        instance_->update_terminal_size();
        instance_->refresh();
    }
}

void TerminalUI::display_message(const std::string& sender, const std::string& content,
                                 bool is_private, bool is_self) {
    DisplayMessage msg;
    msg.sender = sender;
    msg.content = content;
    msg.timestamp = protocol::Timestamp::now();
    msg.is_private = is_private;
    msg.is_system = false;
    msg.is_error = false;
    msg.is_self = is_self;

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        message_history_.push_back(std::move(msg));

        while (message_history_.size() > config_.max_history) {
            message_history_.pop_front();
        }
    }

    if (config_.auto_scroll) {
        scroll_to_bottom();
    }

    refresh();
}

void TerminalUI::display_system_message(const std::string& content) {
    DisplayMessage msg;
    msg.sender = "System";
    msg.content = content;
    msg.timestamp = protocol::Timestamp::now();
    msg.is_private = false;
    msg.is_system = true;
    msg.is_error = false;
    msg.is_self = false;

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        message_history_.push_back(std::move(msg));
    }

    if (config_.auto_scroll) {
        scroll_to_bottom();
    }

    refresh();
}

void TerminalUI::display_error(const std::string& content) {
    DisplayMessage msg;
    msg.sender = "Error";
    msg.content = content;
    msg.timestamp = protocol::Timestamp::now();
    msg.is_private = false;
    msg.is_system = false;
    msg.is_error = true;
    msg.is_self = false;

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        message_history_.push_back(std::move(msg));
    }

    refresh();
}

void TerminalUI::display_user_list(const std::vector<std::pair<UserId, std::string>>& users) {
    std::ostringstream oss;
    oss << "Online users (" << users.size() << "):\n";
    for (const auto& [id, name] : users) {
        oss << "  - " << name << " (#" << id << ")\n";
    }

    display_system_message(oss.str());
}

void TerminalUI::display_help() {
    std::string help = R"(
Available commands:
  /help         - Show this help
  /quit, /exit  - Exit the application
  /connect      - Connect to server
  /disconnect   - Disconnect from server
  /users        - List online users
  /msg <user> <message> - Send private message
  /nick <name>  - Change nickname
  /clear        - Clear screen
  /status       - Show connection status
)";
    display_system_message(help);
}

void TerminalUI::set_status(const std::string& status) {
    status_line_ = status;
    refresh();
}

void TerminalUI::set_connection_status(const std::string& status) {
    connection_status_ = status;
    refresh();
}

void TerminalUI::set_room_info(const std::string& room_name, size_t user_count) {
    room_info_ = room_name + " (" + std::to_string(user_count) + " users)";
    refresh();
}

std::string TerminalUI::get_input() {
    char c;
    while (running_.load()) {
        if (read(STDIN_FILENO, &c, 1) == 1) {
            process_key(c);

            // Check for complete line
            std::lock_guard<std::mutex> lock(input_mutex_);
            if (!input_buffer_.empty() && input_buffer_.back() == '\n') {
                std::string result = input_buffer_.substr(0, input_buffer_.size() - 1);
                input_buffer_.clear();
                cursor_position_ = 0;

                // Add to history
                if (!result.empty()) {
                    input_history_.push_back(result);
                    if (input_history_.size() > 100) {
                        input_history_.pop_front();
                    }
                }
                history_index_ = input_history_.size();

                return result;
            }
        }
    }
    return "";
}

bool TerminalUI::has_input() const {
    std::lock_guard<std::mutex> lock(input_mutex_);
    return !input_buffer_.empty();
}

void TerminalUI::clear_screen() {
    std::cout << colors::CLEAR << colors::HOME << std::flush;
}

void TerminalUI::refresh() {
    if (!running_.load()) return;
    draw_screen();
}

void TerminalUI::resize(int width, int height) {
    width_ = width;
    height_ = height;
    refresh();
}

void TerminalUI::scroll_up(size_t lines) {
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (scroll_offset_ + lines <= message_history_.size()) {
        scroll_offset_ += lines;
    }
    refresh();
}

void TerminalUI::scroll_down(size_t lines) {
    if (scroll_offset_ >= lines) {
        scroll_offset_ -= lines;
    } else {
        scroll_offset_ = 0;
    }
    refresh();
}

void TerminalUI::scroll_to_bottom() {
    scroll_offset_ = 0;
}

void TerminalUI::draw_screen() {
    std::cout << colors::SAVE_CURSOR << colors::HIDE_CURSOR;

    draw_header();
    draw_messages();
    draw_status();
    draw_input_line();

    std::cout << colors::RESTORE_CURSOR << colors::SHOW_CURSOR << std::flush;
}

void TerminalUI::draw_header() {
    move_cursor(1, 1);
    std::cout << colors::REVERSE;

    std::string header = " ChatterBox ";
    if (!connection_status_.empty()) {
        header += "| " + connection_status_ + " ";
    }
    if (!room_info_.empty()) {
        header += "| " + room_info_ + " ";
    }

    // Pad to full width
    while (header.length() < static_cast<size_t>(width_)) {
        header += " ";
    }

    std::cout << header.substr(0, width_) << colors::RESET;
}

void TerminalUI::draw_messages() {
    std::lock_guard<std::mutex> lock(history_mutex_);

    size_t available_lines = height_ - 4; // Header, status, input, separator
    size_t start_idx = 0;

    if (message_history_.size() > available_lines + scroll_offset_) {
        start_idx = message_history_.size() - available_lines - scroll_offset_;
    }

    for (size_t i = 0; i < available_lines; ++i) {
        move_cursor(static_cast<int>(i + 2), 1);
        clear_line();

        size_t msg_idx = start_idx + i;
        if (msg_idx < message_history_.size()) {
            std::string line = format_message(message_history_[msg_idx]);
            std::cout << line.substr(0, width_);
        }
    }
}

void TerminalUI::draw_status() {
    move_cursor(height_ - 1, 1);
    std::cout << colors::DIM;

    std::string status = status_line_;
    while (status.length() < static_cast<size_t>(width_)) {
        status += "-";
    }

    std::cout << status.substr(0, width_) << colors::RESET;
}

void TerminalUI::draw_input_line() {
    move_cursor(height_, 1);
    clear_line();

    std::lock_guard<std::mutex> lock(input_mutex_);
    std::cout << config_.prompt << input_buffer_;

    // Position cursor
    move_cursor(height_, static_cast<int>(config_.prompt.length() + cursor_position_ + 1));
}

void TerminalUI::move_cursor(int row, int col) {
    std::cout << "\033[" << row << ";" << col << "H";
}

void TerminalUI::clear_line() {
    std::cout << colors::CLEAR_LINE;
}

void TerminalUI::process_key(char c) {
    if (c == '\033') {
        // Escape sequence
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return;

        if (seq[0] == '[') {
            handle_special_key(seq[1]);
        }
    } else if (c == 127 || c == '\b') {
        handle_backspace();
    } else if (c == '\r' || c == '\n') {
        handle_enter();
    } else if (c == '\t') {
        handle_tab();
    } else if (c >= 32 && c < 127) {
        // Regular printable character
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_buffer_.insert(cursor_position_, 1, c);
        cursor_position_++;
        refresh();
    }
}

void TerminalUI::handle_special_key(char c) {
    switch (c) {
        case 'A': handle_up_arrow(); break;
        case 'B': handle_down_arrow(); break;
        case 'C': handle_right_arrow(); break;
        case 'D': handle_left_arrow(); break;
        case '3': handle_delete(); break;
        case 'H': // Home
            {
                std::lock_guard<std::mutex> lock(input_mutex_);
                cursor_position_ = 0;
                refresh();
            }
            break;
        case 'F': // End
            {
                std::lock_guard<std::mutex> lock(input_mutex_);
                cursor_position_ = input_buffer_.length();
                refresh();
            }
            break;
        case '5': // Page Up
            scroll_up(10);
            break;
        case '6': // Page Down
            scroll_down(10);
            break;
    }
}

void TerminalUI::handle_backspace() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (cursor_position_ > 0) {
        input_buffer_.erase(cursor_position_ - 1, 1);
        cursor_position_--;
        refresh();
    }
}

void TerminalUI::handle_delete() {
    // Read the tilde that follows '3'
    char c;
    read(STDIN_FILENO, &c, 1);

    std::lock_guard<std::mutex> lock(input_mutex_);
    if (cursor_position_ < input_buffer_.length()) {
        input_buffer_.erase(cursor_position_, 1);
        refresh();
    }
}

void TerminalUI::handle_left_arrow() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (cursor_position_ > 0) {
        cursor_position_--;
        refresh();
    }
}

void TerminalUI::handle_right_arrow() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (cursor_position_ < input_buffer_.length()) {
        cursor_position_++;
        refresh();
    }
}

void TerminalUI::handle_up_arrow() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (history_index_ > 0) {
        history_index_--;
        if (history_index_ < input_history_.size()) {
            input_buffer_ = input_history_[history_index_];
            cursor_position_ = input_buffer_.length();
            refresh();
        }
    }
}

void TerminalUI::handle_down_arrow() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    if (history_index_ < input_history_.size()) {
        history_index_++;
        if (history_index_ < input_history_.size()) {
            input_buffer_ = input_history_[history_index_];
        } else {
            input_buffer_.clear();
        }
        cursor_position_ = input_buffer_.length();
        refresh();
    }
}

void TerminalUI::handle_enter() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_buffer_ += '\n';
}

void TerminalUI::handle_tab() {
    // TODO: Implement auto-completion
}

void TerminalUI::handle_escape() {
    std::lock_guard<std::mutex> lock(input_mutex_);
    input_buffer_.clear();
    cursor_position_ = 0;
    refresh();
}

std::string TerminalUI::format_message(const DisplayMessage& msg) const {
    std::ostringstream oss;

    if (config_.show_timestamps) {
        oss << colors::DIM << "[" << format_timestamp(msg.timestamp) << "] " << colors::RESET;
    }

    if (msg.is_error) {
        oss << colors::RED << colors::BOLD << msg.sender << ": " << colors::RESET;
        oss << colors::RED << msg.content << colors::RESET;
    } else if (msg.is_system) {
        oss << colors::YELLOW << "* " << msg.content << colors::RESET;
    } else if (msg.is_private) {
        if (msg.is_self) {
            oss << colors::MAGENTA << "[PM -> " << msg.sender << "] " << colors::RESET;
        } else {
            oss << colors::MAGENTA << "[PM from " << msg.sender << "] " << colors::RESET;
        }
        oss << msg.content;
    } else {
        if (msg.is_self) {
            oss << colors::CYAN << msg.sender << colors::RESET << ": " << msg.content;
        } else {
            oss << colors::GREEN << msg.sender << colors::RESET << ": " << msg.content;
        }
    }

    return oss.str();
}

std::string TerminalUI::format_timestamp(const protocol::Timestamp& ts) const {
    return ts.to_readable_string();
}

std::string TerminalUI::wrap_text(const std::string& text, size_t width) const {
    std::ostringstream result;
    size_t pos = 0;

    while (pos < text.length()) {
        size_t end = std::min(pos + width, text.length());

        // Find word boundary
        if (end < text.length()) {
            size_t space = text.rfind(' ', end);
            if (space != std::string::npos && space > pos) {
                end = space;
            }
        }

        result << text.substr(pos, end - pos);
        pos = end;

        if (pos < text.length()) {
            result << "\n";
            if (text[pos] == ' ') pos++;
        }
    }

    return result.str();
}

std::string TerminalUI::colorize(const std::string& text, const char* color) const {
    if (!config_.show_colors) {
        return text;
    }
    return std::string(color) + text + colors::RESET;
}

// SimpleUI implementation
void SimpleUI::display_message(const std::string& sender, const std::string& content, bool is_private) {
    std::cout << format_timestamp();
    if (is_private) {
        std::cout << " [PM] ";
    } else {
        std::cout << " ";
    }
    std::cout << sender << ": " << content << std::endl;
}

void SimpleUI::display_system_message(const std::string& content) {
    std::cout << format_timestamp() << " * " << content << std::endl;
}

void SimpleUI::display_error(const std::string& content) {
    std::cerr << format_timestamp() << " ERROR: " << content << std::endl;
}

void SimpleUI::display_user_list(const std::vector<std::pair<UserId, std::string>>& users) {
    std::cout << "Online users (" << users.size() << "):" << std::endl;
    for (const auto& [id, name] : users) {
        std::cout << "  - " << name << " (#" << id << ")" << std::endl;
    }
}

std::string SimpleUI::format_timestamp() const {
    return "[" + protocol::Timestamp::now().to_readable_string() + "]";
}

} // namespace client
} // namespace chatterbox
