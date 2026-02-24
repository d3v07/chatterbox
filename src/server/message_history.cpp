#include <chatterbox/server/message_history.hpp>
#include <fstream>
#include <stdexcept>

namespace chatterbox {
namespace server {

MessageHistory::MessageHistory(const Config& cfg) : cfg_(cfg) {}

MessageHistory::~MessageHistory() = default;

void MessageHistory::evict() {
    // Trim capacity
    while (ring_.size() > cfg_.capacity) {
        ring_.pop_front();
    }
    // Trim age
    if (cfg_.max_age_seconds > 0) {
        auto cutoff = Clock::now() - std::chrono::seconds(cfg_.max_age_seconds);
        while (!ring_.empty() && ring_.front().stored_at < cutoff) {
            ring_.pop_front();
        }
    }
}

void MessageHistory::flush_to_disk(const Entry& entry) {
    if (!cfg_.persist_to_disk) return;
    std::ofstream out(cfg_.persist_path, std::ios::app);
    if (!out) return;
    auto ts = std::chrono::duration_cast<std::chrono::seconds>(
        entry.stored_at.time_since_epoch()).count();
    out << "[" << ts << "] <" << entry.sender_name << "> "
        << entry.msg.payload_as_string() << "\n";
}

void MessageHistory::append(const protocol::Message& msg, const std::string& sender_name) {
    std::lock_guard<std::mutex> lock(mu_);
    Entry e{msg, sender_name, Clock::now(), LOBBY_ROOM_ID};
    flush_to_disk(e);
    ring_.push_back(std::move(e));
    evict();
}

std::vector<protocol::Message> MessageHistory::recent(size_t limit, RoomId /*room*/) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<protocol::Message> result;
    size_t count = std::min(limit, ring_.size());
    result.reserve(count);
    auto it = ring_.end();
    for (size_t i = 0; i < count; ++i) {
        --it;
        result.push_back(it->msg);
    }
    std::reverse(result.begin(), result.end());
    return result;
}

void MessageHistory::replay(size_t limit,
                            const std::function<void(const protocol::Message&)>& cb) const {
    auto msgs = recent(limit);
    for (const auto& m : msgs) cb(m);
}

size_t MessageHistory::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return ring_.size();
}

void MessageHistory::clear() {
    std::lock_guard<std::mutex> lock(mu_);
    ring_.clear();
}

} // namespace server
} // namespace chatterbox
