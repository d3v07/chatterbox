#ifndef CHATTERBOX_SERVER_CONFIG_PARSER_HPP
#define CHATTERBOX_SERVER_CONFIG_PARSER_HPP

#include <string>
#include <unordered_map>
#include <stdexcept>
#include <sstream>

namespace chatterbox {
namespace server {

// Minimal INI-style config file parser.
// Supports [section] headers and key = value pairs.
// Lines starting with '#' or ';' are treated as comments.
//
// Example file:
//   [server]
//   max_users = 32
//   thread_pool_size = 4
//   [rate_limit]
//   max_messages = 10
//   window_ms = 1000
class ConfigParser {
public:
    class ParseError : public std::runtime_error {
    public:
        explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
    };

    ConfigParser() = default;

    // Load and parse an INI file. Throws ParseError on syntax errors.
    void load(const std::string& path);

    // Parse from a string (useful for tests).
    void parse(const std::string& text);

    // Retrieve a value. Returns def if the key is absent.
    std::string get(const std::string& section,
                    const std::string& key,
                    const std::string& def = "") const;

    int         get_int   (const std::string& section, const std::string& key, int def = 0) const;
    double      get_double(const std::string& section, const std::string& key, double def = 0.0) const;
    bool        get_bool  (const std::string& section, const std::string& key, bool def = false) const;

    bool has(const std::string& section, const std::string& key) const;

    // Dump current contents for debugging.
    std::string dump() const;

private:
    // Storage: section -> key -> value
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::string>> data_;

    static std::string trim(const std::string& s);
    static std::string make_key(const std::string& section, const std::string& key);
    void parse_line(const std::string& line, std::string& current_section, size_t lineno);
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_CONFIG_PARSER_HPP
