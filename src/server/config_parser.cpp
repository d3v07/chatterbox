#include <chatterbox/server/config_parser.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace chatterbox {
namespace server {

std::string ConfigParser::trim(const std::string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

void ConfigParser::parse_line(const std::string& raw_line,
                               std::string& current_section,
                               size_t lineno) {
    std::string line = trim(raw_line);

    // Skip empty lines and comments
    if (line.empty() || line[0] == '#' || line[0] == ';') return;

    // Section header
    if (line[0] == '[') {
        auto close = line.find(']');
        if (close == std::string::npos) {
            throw ParseError("line " + std::to_string(lineno) + ": unterminated section header");
        }
        current_section = trim(line.substr(1, close - 1));
        return;
    }

    // Key = value
    auto eq = line.find('=');
    if (eq == std::string::npos) {
        throw ParseError("line " + std::to_string(lineno) + ": expected '=' in assignment");
    }

    std::string key   = trim(line.substr(0, eq));
    std::string value = trim(line.substr(eq + 1));

    // Strip inline comment after value
    for (char c : {'#', ';'}) {
        auto pos = value.find(c);
        if (pos != std::string::npos) value = trim(value.substr(0, pos));
    }

    data_[current_section][key] = value;
}

void ConfigParser::parse(const std::string& text) {
    std::istringstream ss(text);
    std::string line;
    std::string section;
    size_t lineno = 0;
    while (std::getline(ss, line)) {
        ++lineno;
        parse_line(line, section, lineno);
    }
}

void ConfigParser::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw ParseError("cannot open config file: " + path);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    parse(content);
}

std::string ConfigParser::get(const std::string& section,
                               const std::string& key,
                               const std::string& def) const {
    auto sit = data_.find(section);
    if (sit == data_.end()) return def;
    auto kit = sit->second.find(key);
    if (kit == sit->second.end()) return def;
    return kit->second;
}

int ConfigParser::get_int(const std::string& section, const std::string& key, int def) const {
    std::string v = get(section, key);
    if (v.empty()) return def;
    try { return std::stoi(v); } catch (...) { return def; }
}

double ConfigParser::get_double(const std::string& section, const std::string& key, double def) const {
    std::string v = get(section, key);
    if (v.empty()) return def;
    try { return std::stod(v); } catch (...) { return def; }
}

bool ConfigParser::get_bool(const std::string& section, const std::string& key, bool def) const {
    std::string v = get(section, key);
    if (v.empty()) return def;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

bool ConfigParser::has(const std::string& section, const std::string& key) const {
    auto sit = data_.find(section);
    if (sit == data_.end()) return false;
    return sit->second.count(key) > 0;
}

std::string ConfigParser::dump() const {
    std::ostringstream os;
    for (const auto& [section, keys] : data_) {
        os << "[" << section << "]\n";
        for (const auto& [k, v] : keys) {
            os << "  " << k << " = " << v << "\n";
        }
    }
    return os.str();
}

} // namespace server
} // namespace chatterbox
