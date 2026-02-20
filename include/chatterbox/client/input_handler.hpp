#ifndef CHATTERBOX_CLIENT_INPUT_HANDLER_HPP
#define CHATTERBOX_CLIENT_INPUT_HANDLER_HPP

#include <chatterbox/common.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <regex>

namespace chatterbox {
namespace client {

// Forward declaration
class Client;

// Command argument type
struct CommandArg {
    std::string value;
    bool is_optional;
    std::string default_value;

    std::string as_string() const { return value.empty() ? default_value : value; }
    int as_int(int def = 0) const;
    UserId as_user_id() const;
    bool as_bool() const;
};

// Parsed command
struct ParsedCommand {
    std::string name;
    std::vector<CommandArg> args;
    std::string raw_args; // Everything after the command name
    bool is_valid;
    std::string error;
};

// Command handler function type
using CommandHandler = std::function<void(Client&, const ParsedCommand&)>;

// Command definition
struct CommandDefinition {
    std::string name;
    std::string description;
    std::string usage;
    std::vector<std::string> aliases;
    CommandHandler handler;
    bool requires_connection;
    int min_args;
    int max_args; // -1 for unlimited
};

// Handles user input and commands
class InputHandler {
public:
    explicit InputHandler(Client& client);
    ~InputHandler() = default;

    // Non-copyable
    InputHandler(const InputHandler&) = delete;
    InputHandler& operator=(const InputHandler&) = delete;

    // Process input line
    void process_input(const std::string& input);

    // Register commands
    void register_command(const CommandDefinition& cmd);
    void unregister_command(const std::string& name);

    // Register default commands
    void register_default_commands();

    // Get command help
    std::string get_help(const std::string& command_name = "") const;
    std::vector<std::string> get_command_list() const;

    // Command completion
    std::vector<std::string> get_completions(const std::string& partial) const;

    // Input validation
    bool is_command(const std::string& input) const;
    ParsedCommand parse_command(const std::string& input) const;

    // Set command prefix (default is '/')
    void set_command_prefix(char prefix) { command_prefix_ = prefix; }
    char command_prefix() const { return command_prefix_; }

private:
    Client& client_;
    char command_prefix_ = '/';

    mutable std::mutex commands_mutex_;
    std::unordered_map<std::string, CommandDefinition> commands_;
    std::unordered_map<std::string, std::string> aliases_; // alias -> command name

    // Parse helpers
    std::vector<std::string> tokenize(const std::string& input) const;
    std::string trim(const std::string& str) const;

    // Built-in command handlers
    static void cmd_help(Client& client, const ParsedCommand& cmd);
    static void cmd_quit(Client& client, const ParsedCommand& cmd);
    static void cmd_connect(Client& client, const ParsedCommand& cmd);
    static void cmd_disconnect(Client& client, const ParsedCommand& cmd);
    static void cmd_msg(Client& client, const ParsedCommand& cmd);
    static void cmd_users(Client& client, const ParsedCommand& cmd);
    static void cmd_join(Client& client, const ParsedCommand& cmd);
    static void cmd_leave(Client& client, const ParsedCommand& cmd);
    static void cmd_nick(Client& client, const ParsedCommand& cmd);
    static void cmd_clear(Client& client, const ParsedCommand& cmd);
    static void cmd_status(Client& client, const ParsedCommand& cmd);
};

// Input validation utilities
class InputValidator {
public:
    // Validate username
    static bool is_valid_username(const std::string& username);
    static std::string sanitize_username(const std::string& username);

    // Validate message content
    static bool is_valid_message(const std::string& content);
    static std::string sanitize_message(const std::string& content);

    // Validate room name
    static bool is_valid_room_name(const std::string& name);

    // Parse user reference (username or @username or #userid)
    static std::pair<bool, std::string> parse_user_reference(const std::string& ref);

    // Parse mentions in message
    static std::vector<std::string> extract_mentions(const std::string& message);

private:
    static const std::regex username_pattern_;
    static const std::regex room_name_pattern_;
    static const std::regex mention_pattern_;
};

// Auto-complete provider
class AutoComplete {
public:
    explicit AutoComplete(const InputHandler& handler);

    // Get completions for partial input
    std::vector<std::string> complete(const std::string& input, size_t cursor_pos) const;

    // Add completion source
    void add_username(const std::string& username);
    void remove_username(const std::string& username);
    void clear_usernames();

    void add_room_name(const std::string& room);
    void remove_room_name(const std::string& room);
    void clear_room_names();

private:
    const InputHandler& handler_;

    mutable std::mutex mutex_;
    std::vector<std::string> usernames_;
    std::vector<std::string> room_names_;

    std::vector<std::string> complete_command(const std::string& partial) const;
    std::vector<std::string> complete_username(const std::string& partial) const;
    std::vector<std::string> complete_room(const std::string& partial) const;
};

} // namespace client
} // namespace chatterbox

#endif // CHATTERBOX_CLIENT_INPUT_HANDLER_HPP
