#ifndef CHATTERBOX_SERVER_METRICS_EXPORTER_HPP
#define CHATTERBOX_SERVER_METRICS_EXPORTER_HPP

#include <chatterbox/server/server.hpp>
#include <string>

namespace chatterbox {
namespace server {

// Formats a ServerStats snapshot in various serialization formats.
// Primarily used to expose metrics to external monitoring systems.
class MetricsExporter {
public:
    enum class Format { JSON, PROMETHEUS, TEXT };

    explicit MetricsExporter(const Server& server, Format fmt = Format::JSON);

    // Render current stats as a string in the configured format.
    std::string snapshot() const;

    // Write snapshot to a file (useful for scraping by external agents).
    void write_to_file(const std::string& path) const;

private:
    const Server& server_;
    Format        format_;

    std::string to_json(const ServerStats& s) const;
    std::string to_prometheus(const ServerStats& s) const;
    std::string to_text(const ServerStats& s) const;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_METRICS_EXPORTER_HPP
