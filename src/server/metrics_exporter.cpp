#include <chatterbox/server/metrics_exporter.hpp>
#include <sstream>
#include <fstream>
#include <iomanip>

namespace chatterbox {
namespace server {

MetricsExporter::MetricsExporter(const Server& server, Format fmt)
    : server_(server), format_(fmt) {}

std::string MetricsExporter::to_json(const ServerStats& s) const {
    std::ostringstream os;
    os << std::fixed << std::setprecision(3);
    os << "{\n"
       << "  \"messages_received\": " << s.messages_received << ",\n"
       << "  \"messages_sent\": "     << s.messages_sent     << ",\n"
       << "  \"messages_dropped\": "  << s.messages_dropped  << ",\n"
       << "  \"messages_routed\": "   << s.messages_routed   << ",\n"
       << "  \"bytes_received\": "    << s.bytes_received    << ",\n"
       << "  \"bytes_sent\": "        << s.bytes_sent        << ",\n"
       << "  \"total_connections\": " << s.total_connections << ",\n"
       << "  \"connected_users\": "   << s.connected_users   << ",\n"
       << "  \"peak_users\": "        << s.peak_users        << ",\n"
       << "  \"timeout_disconnects\": " << s.timeout_disconnects << ",\n"
       << "  \"avg_latency_ms\": "    << s.avg_latency_ms   << ",\n"
       << "  \"max_latency_ms\": "    << s.max_latency_ms   << ",\n"
       << "  \"uptime_seconds\": "    << s.uptime_seconds    << "\n"
       << "}\n";
    return os.str();
}

std::string MetricsExporter::to_prometheus(const ServerStats& s) const {
    std::ostringstream os;
    auto g = [&](const char* name, uint64_t v, const char* help) {
        os << "# HELP " << name << " " << help << "\n"
           << "# TYPE " << name << " gauge\n"
           << name << " " << v << "\n";
    };
    g("chatterbox_messages_received_total",  s.messages_received,  "Total messages received by server");
    g("chatterbox_messages_sent_total",      s.messages_sent,      "Total messages sent by server");
    g("chatterbox_messages_dropped_total",   s.messages_dropped,   "Total messages dropped");
    g("chatterbox_connected_users",          s.connected_users,    "Currently connected users");
    g("chatterbox_peak_users",               s.peak_users,         "Peak concurrent users");
    g("chatterbox_uptime_seconds",           s.uptime_seconds,     "Server uptime in seconds");
    os << "# HELP chatterbox_avg_latency_ms Average message latency in ms\n"
       << "# TYPE chatterbox_avg_latency_ms gauge\n"
       << "chatterbox_avg_latency_ms " << std::fixed << std::setprecision(3)
       << s.avg_latency_ms << "\n";
    return os.str();
}

std::string MetricsExporter::to_text(const ServerStats& s) const {
    std::ostringstream os;
    os << "ChatterBox Server Metrics\n"
       << "=========================\n"
       << "Messages received : " << s.messages_received  << "\n"
       << "Messages sent     : " << s.messages_sent      << "\n"
       << "Messages dropped  : " << s.messages_dropped   << "\n"
       << "Connected users   : " << s.connected_users    << "\n"
       << "Peak users        : " << s.peak_users         << "\n"
       << "Avg latency (ms)  : " << std::fixed << std::setprecision(2) << s.avg_latency_ms << "\n"
       << "Uptime (seconds)  : " << s.uptime_seconds     << "\n";
    return os.str();
}

std::string MetricsExporter::snapshot() const {
    ServerStats s = server_.get_stats();
    switch (format_) {
        case Format::JSON:       return to_json(s);
        case Format::PROMETHEUS: return to_prometheus(s);
        case Format::TEXT:       return to_text(s);
    }
    return {};
}

void MetricsExporter::write_to_file(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (out) out << snapshot();
}

} // namespace server
} // namespace chatterbox
