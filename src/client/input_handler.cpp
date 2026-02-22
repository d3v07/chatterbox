#include <chatterbox/client/input_handler.hpp>
#include <chatterbox/client/client.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace chatterbox {
namespace client {

// Static regex patterns
const std::regex InputValidator::username_pattern_("^[a-zA-Z][a-zA-Z0-9_]{2,31}$");
const std::regex InputValidator::room_name_pattern_("^[a-zA-Z][a-zA-Z0-9_-]{2,63}$");
const std::regex InputValidator::mention_pattern_("@([a-zA-Z][a-zA-Z0-9_]{2,31})");

int CommandArg::as_int(int def) const {
    try {
        return std::stoi(value);
    } catch (...) {
        return def;
    }
}

UserId CommandArg::as_user_id() const {
    return static_cast<UserId>(as_int(0));
}

bool CommandArg::as_bool() const {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower == "true" || lower == "yes" || lower == "1" || lower == "on";
}

InputHandler::InputHandler(Client& client)
    : client_(client)
{
}

void InputHandler::register_command(const CommandDefinition& cmd) {
    std::lock_guard<std::mutex> lock(commands_mutex_);
    commands_[cmd.name] = cmd;

    for (const auto& alias : cmd.aliases) {
        aliases_[alias] = cmd.name;
    }
}

void InputHandler::unregister_command(const std::string& name) {
    std::lock_guard<std::mutex> lock(commands_mutex_);

    auto it = commands_.find(name);
    if (it != commands_.end()) {
        // Remove aliases
        for (const auto& alias : it->second.aliases) {
            aliases_.erase(alias);
        }
        commands_.erase(it);
    }
}

void InputHandler::register_default_commands() {
    register_command({
        "help", "Show help information", "/help [command]",
        {"h", "?"},
        cmd_help,
        false, 0, 1
    });

    register_command({
        "quit", "Exit the application", "/quit",
        {"exit", "q"},
        cmd_quit,
        false, 0, 0
    });

    register_command({
        "connect", "Connect to server", "/connect [username]",
        {"c"},
        cmd_connect,
        false, 0, 1
    });

    register_command({
        "disconnect", "Disconnect from server", "/disconnect",
        {"dc"},
        cmd_disconnect,
        true, 0, 0
    });

    register_command({
        "msg", "Send a private message", "/msg <username> <message>",
        {"pm", "whisper", "w"},
        cmd_msg,
        true, 2, -1
    });

    register_command({
        "users", "List online users", "/users",
        {"who", "list"},
        cmd_users,
        true, 0, 0
    });

    register_command({
        "join", "Join a room", "/join <room>",
        {"j"},
        cmd_join,
        true, 1, 1
    });

    register_command({
        "leave", "Leave current room", "/leave",
        {"part"},
        cmd_leave,
        true, 0, 0
    });

    register_command({
        "nick", "Change nickname", "/nick <name>",
        {"name"},
        cmd_nick,
        false, 1, 1
    });

    register_command({
        "clear", "Clear the screen", "/clear",
        {"cls"},
        cmd_clear,
        false, 0, 0
    });

    register_command({
        "status", "Show connection status", "/status",
        {"stat"},
        cmd_status,
        false, 0, 0
    });
}

void InputHandler::process_input(const std::string& input) {
    if (!is_command(input)) {
        // Regular message - should be handled by client
        return;
    }

    ParsedCommand cmd = parse_command(input);
    if (!cmd.is_valid) {
        if (client_.ui()) {
            client_.ui()->display_error(cmd.error);
        }
        return;
    }

    std::lock_guard<std::mutex> lock(commands_mutex_);

    // Resolve alias
    std::string cmd_name = cmd.name;
    auto alias_it = aliases_.find(cmd_name);
    if (alias_it != aliases_.end()) {
        cmd_name = alias_it->second;
    }

    auto it = commands_.find(cmd_name);
    if (it == commands_.end()) {
        if (client_.ui()) {
            client_.ui()->display_error("Unknown command: " + cmd.name);
        }
        return;
    }

    const auto& def = it->second;

    // Check connection requirement
    if (def.requires_connection && !client_.is_connected()) {
        if (client_.ui()) {
            client_.ui()->display_error("This command requires an active connection.");
        }
        return;
    }

    // Check argument count
    int arg_count = static_cast<int>(cmd.args.size());
    if (arg_count < def.min_args) {
        if (client_.ui()) {
            client_.ui()->display_error("Not enough arguments. Usage: " + def.usage);
        }
        return;
    }
    if (def.max_args >= 0 && arg_count > def.max_args) {
        if (client_.ui()) {
            client_.ui()->display_error("Too many arguments. Usage: " + def.usage);
        }
        return;
    }

    // Execute handler
    if (def.handler) {
        def.handler(client_, cmd);
    }
}

std::string InputHandler::get_help(const std::string& command_name) const {
    std::lock_guard<std::mutex> lock(commands_mutex_);

    if (command_name.empty()) {
        std::ostringstream oss;
        oss << "Available commands:\n";
        for (const auto& [name, def] : commands_) {
            oss << "  " << def.usage << " - " << def.description << "\n";
        }
        return oss.str();
    }

    std::string name = command_name;
    auto alias_it = aliases_.find(name);
    if (alias_it != aliases_.end()) {
        name = alias_it->second;
    }

    auto it = commands_.find(name);
    if (it == commands_.end()) {
        return "Unknown command: " + command_name;
    }

    const auto& def = it->second;
    std::ostringstream oss;
    oss << "Command: " << def.name << "\n";
    oss << "Description: " << def.description << "\n";
    oss << "Usage: " << def.usage << "\n";
    if (!def.aliases.empty()) {
        oss << "Aliases: ";
        for (size_t i = 0; i < def.aliases.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << def.aliases[i];
        }
        oss << "\n";
    }
    return oss.str();
}

std::vector<std::string> InputHandler::get_command_list() const {
    std::lock_guard<std::mutex> lock(commands_mutex_);
    std::vector<std::string> result;
    result.reserve(commands_.size());
    for (const auto& [name, _] : commands_) {
        result.push_back(name);
    }
    return result;
}

std::vector<std::string> InputHandler::get_completions(const std::string& partial) const {
    std::vector<std::string> result;

    if (partial.empty() || partial[0] != command_prefix_) {
        return result;
    }

    std::string cmd_part = partial.substr(1);
    std::lock_guard<std::mutex> lock(commands_mutex_);

    for (const auto& [name, _] : commands_) {
        if (name.find(cmd_part) == 0) {
            result.push_back(std::string(1, command_prefix_) + name);
        }
    }

    for (const auto& [alias, _] : aliases_) {
        if (alias.find(cmd_part) == 0) {
            result.push_back(std::string(1, command_prefix_) + alias);
        }
    }

    return result;
}

bool InputHandler::is_command(const std::string& input) const {
    return !input.empty() && input[0] == command_prefix_;
}

ParsedCommand InputHandler::parse_command(const std::string& input) const {
    ParsedCommand result;
    result.is_valid = false;

    if (!is_command(input)) {
        result.error = "Not a command";
        return result;
    }

    std::vector<std::string> tokens = tokenize(input.substr(1));
    if (tokens.empty()) {
        result.error = "Empty command";
        return result;
    }

    result.name = tokens[0];
    std::transform(result.name.begin(), result.name.end(), result.name.begin(), ::tolower);

    // Extract arguments
    for (size_t i = 1; i < tokens.size(); ++i) {
        CommandArg arg;
        arg.value = tokens[i];
        arg.is_optional = false;
        result.args.push_back(std::move(arg));
    }

    // Extract raw args (everything after command name)
    size_t cmd_end = input.find(' ');
    if (cmd_end != std::string::npos) {
        result.raw_args = trim(input.substr(cmd_end + 1));
    }

    result.is_valid = true;
    return result;
}

std::vector<std::string> InputHandler::tokenize(const std::string& input) const {
    std::vector<std::string> tokens;
    std::istringstream iss(input);
    std::string token;

    bool in_quotes = false;
    std::string current;

    for (char c : input) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }

    if (!current.empty()) {
        tokens.push_back(current);
    }

    return tokens;
}

std::string InputHandler::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";

    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

// Built-in command handlers
void InputHandler::cmd_help(Client& client, const ParsedCommand& cmd) {
    if (cmd.args.empty()) {
        client.ui()->display_help();
    } else {
        // Show help for specific command
        // TODO: Get help from input handler
        client.ui()->display_system_message("Help for: " + cmd.args[0].value);
    }
}

void InputHandler::cmd_quit(Client& client, const ParsedCommand& cmd) {
    (void)cmd;
    client.disconnect("User quit");
    client.stop_ui();
}

void InputHandler::cmd_connect(Client& client, const ParsedCommand& cmd) {
    std::string username;
    if (!cmd.args.empty()) {
        username = cmd.args[0].value;
    }

    auto err = client.connect(username);
    if (err != ErrorCode::SUCCESS) {
        client.ui()->display_error("Connection failed: " + std::string(error_to_string(err)));
    }
}

void InputHandler::cmd_disconnect(Client& client, const ParsedCommand& cmd) {
    (void)cmd;
    client.disconnect("User requested disconnect");
}

void InputHandler::cmd_msg(Client& client, const ParsedCommand& cmd) {
    if (cmd.args.size() < 2) {
        client.ui()->display_error("Usage: /msg <username> <message>");
        return;
    }

    std::string target = cmd.args[0].value;

    // Build message from remaining args
    std::ostringstream oss;
    for (size_t i = 1; i < cmd.args.size(); ++i) {
        if (i > 1) oss << " ";
        oss << cmd.args[i].value;
    }

    auto err = client.send_private_message(target, oss.str());
    if (err != ErrorCode::SUCCESS) {
        client.ui()->display_error("Failed to send message: " + std::string(error_to_string(err)));
    }
}

void InputHandler::cmd_users(Client& client, const ParsedCommand& cmd) {
    (void)cmd;
    client.request_user_list();
}

void InputHandler::cmd_join(Client& client, const ParsedCommand& cmd) {
    if (cmd.args.empty()) {
        client.ui()->display_error("Usage: /join <room>");
        return;
    }

    // Parse room ID or name
    std::string room_arg = cmd.args[0].value;
    RoomId room_id;

    try {
        room_id = static_cast<RoomId>(std::stoul(room_arg));
    } catch (...) {
        // TODO: Look up room by name
        client.ui()->display_error("Room not found: " + room_arg);
        return;
    }

    auto err = client.join_room(room_id);
    if (err != ErrorCode::SUCCESS) {
        client.ui()->display_error("Failed to join room: " + std::string(error_to_string(err)));
    }
}

void InputHandler::cmd_leave(Client& client, const ParsedCommand& cmd) {
    (void)cmd;
    auto err = client.leave_room();
    if (err != ErrorCode::SUCCESS) {
        client.ui()->display_error("Failed to leave room: " + std::string(error_to_string(err)));
    }
}

void InputHandler::cmd_nick(Client& client, const ParsedCommand& cmd) {
    if (cmd.args.empty()) {
        client.ui()->display_error("Usage: /nick <name>");
        return;
    }

    std::string new_name = cmd.args[0].value;
    if (!InputValidator::is_valid_username(new_name)) {
        client.ui()->display_error("Invalid username. Must be 3-32 characters, alphanumeric.");
        return;
    }

    client.set_username(new_name);
    client.ui()->display_system_message("Nickname set to: " + new_name);

    // Reconnect if connected
    if (client.is_connected()) {
        client.ui()->display_system_message("Reconnecting with new nickname...");
        client.disconnect("Changing nickname");
        client.connect();
    }
}

void InputHandler::cmd_clear(Client& client, const ParsedCommand& cmd) {
    (void)cmd;
    client.ui()->clear_screen();
}

void InputHandler::cmd_status(Client& client, const ParsedCommand& cmd) {
    (void)cmd;

    std::ostringstream oss;
    oss << "Connection Status:\n";

    switch (client.state()) {
        case ClientState::DISCONNECTED:
            oss << "  State: Disconnected\n";
            break;
        case ClientState::CONNECTING:
            oss << "  State: Connecting...\n";
            break;
        case ClientState::CONNECTED:
            oss << "  State: Connected\n";
            oss << "  User ID: " << client.user_id() << "\n";
            oss << "  Username: " << client.username() << "\n";
            break;
        case ClientState::RECONNECTING:
            oss << "  State: Reconnecting...\n";
            break;
        case ClientState::ERROR:
            oss << "  State: Error\n";
            break;
    }

    auto stats = client.get_stats();
    oss << "Statistics:\n";
    oss << "  Messages sent: " << stats.messages_sent << "\n";
    oss << "  Messages received: " << stats.messages_received << "\n";
    oss << "  Average latency: " << stats.avg_latency_ms << " ms\n";
    oss << "  Reconnect count: " << stats.reconnect_count << "\n";

    client.ui()->display_system_message(oss.str());
}

// InputValidator implementation
bool InputValidator::is_valid_username(const std::string& username) {
    return std::regex_match(username, username_pattern_);
}

std::string InputValidator::sanitize_username(const std::string& username) {
    std::string result;
    for (char c : username) {
        if (std::isalnum(c) || c == '_') {
            result += c;
        }
    }
    if (result.length() < 3) {
        result = "User" + result;
    }
    if (result.length() > 32) {
        result = result.substr(0, 32);
    }
    return result;
}

bool InputValidator::is_valid_message(const std::string& content) {
    // Basic validation - not empty, not too long
    return !content.empty() && content.length() <= MAX_PAYLOAD_SIZE;
}

std::string InputValidator::sanitize_message(const std::string& content) {
    std::string result = content;
    if (result.length() > MAX_PAYLOAD_SIZE) {
        result = result.substr(0, MAX_PAYLOAD_SIZE);
    }
    return result;
}

bool InputValidator::is_valid_room_name(const std::string& name) {
    return std::regex_match(name, room_name_pattern_);
}

std::pair<bool, std::string> InputValidator::parse_user_reference(const std::string& ref) {
    if (ref.empty()) {
        return {false, ""};
    }

    if (ref[0] == '@') {
        return {true, ref.substr(1)};
    }

    if (ref[0] == '#') {
        // Numeric ID
        return {true, ref.substr(1)};
    }

    return {true, ref};
}

std::vector<std::string> InputValidator::extract_mentions(const std::string& message) {
    std::vector<std::string> mentions;
    std::sregex_iterator it(message.begin(), message.end(), mention_pattern_);
    std::sregex_iterator end;

    while (it != end) {
        mentions.push_back((*it)[1].str());
        ++it;
    }

    return mentions;
}

// AutoComplete implementation
AutoComplete::AutoComplete(const InputHandler& handler)
    : handler_(handler)
{
}

std::vector<std::string> AutoComplete::complete(const std::string& input, size_t cursor_pos) const {
    if (input.empty()) {
        return {};
    }

    // Determine what we're completing
    std::string partial = input.substr(0, cursor_pos);

    // Find the word being completed
    size_t word_start = partial.rfind(' ');
    std::string word;
    if (word_start == std::string::npos) {
        word = partial;
    } else {
        word = partial.substr(word_start + 1);
    }

    if (word.empty()) {
        return {};
    }

    // Command completion
    if (word[0] == '/') {
        return complete_command(word);
    }

    // Username completion (for @mentions)
    if (word[0] == '@') {
        return complete_username(word.substr(1));
    }

    // Room completion (for #rooms)
    if (word[0] == '#') {
        return complete_room(word.substr(1));
    }

    // General username completion
    return complete_username(word);
}

void AutoComplete::add_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(usernames_.begin(), usernames_.end(), username) == usernames_.end()) {
        usernames_.push_back(username);
    }
}

void AutoComplete::remove_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    usernames_.erase(std::remove(usernames_.begin(), usernames_.end(), username), usernames_.end());
}

void AutoComplete::clear_usernames() {
    std::lock_guard<std::mutex> lock(mutex_);
    usernames_.clear();
}

void AutoComplete::add_room_name(const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (std::find(room_names_.begin(), room_names_.end(), room) == room_names_.end()) {
        room_names_.push_back(room);
    }
}

void AutoComplete::remove_room_name(const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex_);
    room_names_.erase(std::remove(room_names_.begin(), room_names_.end(), room), room_names_.end());
}

void AutoComplete::clear_room_names() {
    std::lock_guard<std::mutex> lock(mutex_);
    room_names_.clear();
}

std::vector<std::string> AutoComplete::complete_command(const std::string& partial) const {
    return handler_.get_completions(partial);
}

std::vector<std::string> AutoComplete::complete_username(const std::string& partial) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    for (const auto& name : usernames_) {
        if (name.find(partial) == 0) {
            result.push_back(name);
        }
    }

    return result;
}

std::vector<std::string> AutoComplete::complete_room(const std::string& partial) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;

    for (const auto& name : room_names_) {
        if (name.find(partial) == 0) {
            result.push_back("#" + name);
        }
    }

    return result;
}

} // namespace client
} // namespace chatterbox
