#ifndef CHATTERBOX_CLIENT_TERMINAL_UI_HPP
#define CHATTERBOX_CLIENT_TERMINAL_UI_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/protocol/message.hpp>
#include <chatterbox/protocol/timestamp.hpp>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>
#include <termios.h>

namespace chatterbox {
namespace client {

// Color codes for terminal output
namespace colors {
    constexpr const char* RESET = "\033[0m";
    constexpr const char* RED = "\033[31m";
    constexpr const char* GREEN = "\033[32m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* BLUE = "\033[34m";
    constexpr const char* MAGENTA = "\033[35m";
    constexpr const char* CYAN = "\033[36m";
    constexpr const char* WHITE = "\033[37m";
    constexpr const char* BOLD = "\033[1m";
    constexpr const char* DIM = "\033[2m";
    constexpr const char* UNDERLINE = "\033[4m";
    constexpr const char* BLINK = "\033[5m";
    constexpr const char* REVERSE = "\033[7m";

    // Clear screen
    constexpr const char* CLEAR = "\033[2J";
    constexpr const char* CLEAR_LINE = "\033[2K";

    // Cursor movement
    constexpr const char* HOME = "\033[H";
    constexpr const char* SAVE_CURSOR = "\033[s";
    constexpr const char* RESTORE_CURSOR = "\033[u";
    constexpr const char* HIDE_CURSOR = "\033[?25l";
    constexpr const char* SHOW_CURSOR = "\033[?25h";
}

// Message display entry
struct DisplayMessage {
    std::string sender;
    std::string content;
    protocol::Timestamp timestamp;
    bool is_private;
    bool is_system;
    bool is_error;
    bool is_self;
};

// UI configuration
struct UIConfig {
    size_t max_history = 1000;
    size_t visible_lines = 20;
    bool show_timestamps = true;
    bool show_colors = true;
    bool auto_scroll = true;
    std::string prompt = "> ";
};

// Terminal UI for the chat client
class TerminalUI {
public:
    explicit TerminalUI(const UIConfig& config = UIConfig());
    ~TerminalUI();

    // Non-copyable
    TerminalUI(const TerminalUI&) = delete;
    TerminalUI& operator=(const TerminalUI&) = delete;

    // Lifecycle
    void start();
    void stop();
    bool is_running() const { return running_.load(); }

    // Display messages
    void display_message(const std::string& sender, const std::string& content,
                        bool is_private = false, bool is_self = false);
    void display_system_message(const std::string& content);
    void display_error(const std::string& content);
    void display_user_list(const std::vector<std::pair<UserId, std::string>>& users);
    void display_help();

    // Status line
    void set_status(const std::string& status);
    void set_connection_status(const std::string& status);
    void set_room_info(const std::string& room_name, size_t user_count);

    // Input handling
    std::string get_input();
    bool has_input() const;

    // Screen control
    void clear_screen();
    void refresh();
    void resize(int width, int height);

    // Scrolling
    void scroll_up(size_t lines = 1);
    void scroll_down(size_t lines = 1);
    void scroll_to_bottom();

    // Configuration
    const UIConfig& config() const { return config_; }
    void set_config(const UIConfig& config) { config_ = config; }

    // Terminal size
    int terminal_width() const { return width_; }
    int terminal_height() const { return height_; }

    // Input callback
    using InputCallback = std::function<void(const std::string&)>;
    void set_input_callback(InputCallback callback) { input_callback_ = std::move(callback); }

private:
    UIConfig config_;
    std::atomic<bool> running_{false};

    // Terminal state
    int width_;
    int height_;
    struct termios original_termios_;
    bool raw_mode_enabled_;

    // Message history
    mutable std::mutex history_mutex_;
    std::deque<DisplayMessage> message_history_;
    size_t scroll_offset_;

    // Input buffer
    mutable std::mutex input_mutex_;
    std::string input_buffer_;
    size_t cursor_position_;
    std::deque<std::string> input_history_;
    size_t history_index_;

    // Status information
    std::string status_line_;
    std::string connection_status_;
    std::string room_info_;

    // Callbacks
    InputCallback input_callback_;

    // Internal methods
    void enable_raw_mode();
    void disable_raw_mode();
    void update_terminal_size();
    void draw_screen();
    void draw_header();
    void draw_messages();
    void draw_status();
    void draw_input_line();
    void move_cursor(int row, int col);
    void clear_line();

    // Input processing
    void process_key(char c);
    void handle_special_key(char c);
    void handle_backspace();
    void handle_delete();
    void handle_left_arrow();
    void handle_right_arrow();
    void handle_up_arrow();
    void handle_down_arrow();
    void handle_enter();
    void handle_tab();
    void handle_escape();

    // Formatting
    std::string format_message(const DisplayMessage& msg) const;
    std::string format_timestamp(const protocol::Timestamp& ts) const;
    std::string wrap_text(const std::string& text, size_t width) const;
    std::string colorize(const std::string& text, const char* color) const;

    // Signal handling
    static void handle_resize_signal(int sig);
    static TerminalUI* instance_;
};

// Simple text-only UI for non-interactive use
class SimpleUI {
public:
    SimpleUI() = default;

    void display_message(const std::string& sender, const std::string& content, bool is_private = false);
    void display_system_message(const std::string& content);
    void display_error(const std::string& content);
    void display_user_list(const std::vector<std::pair<UserId, std::string>>& users);

private:
    std::string format_timestamp() const;
};

} // namespace client
} // namespace chatterbox

#endif // CHATTERBOX_CLIENT_TERMINAL_UI_HPP
