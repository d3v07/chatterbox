#ifndef CHATTERBOX_SERVER_MESSAGE_HISTORY_HPP
#define CHATTERBOX_SERVER_MESSAGE_HISTORY_HPP

#include <chatterbox/common.hpp>
#include <chatterbox/protocol/message.hpp>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <functional>

namespace chatterbox {
namespace server {

// Holds a bounded ring of recent messages for a channel or the global lobby.
// Messages older than max_age_seconds are discarded on each append.
class MessageHistory {
public:
    struct Config {
        size_t  capacity        = 500;   // max messages kept in memory
        int64_t max_age_seconds = 3600;  // drop messages older than this
        bool    persist_to_disk = false;
        std::string persist_path = "chatterbox_history.log";
    };

    explicit MessageHistory(const Config& cfg = Config());
    ~MessageHistory();

    // Append a message. Evicts old/excess entries first.
    void append(const protocol::Message& msg, const std::string& sender_name);

    // Return at most 'limit' recent messages for the given room
    // (LOBBY_ROOM_ID = 0 means global).
    std::vector<protocol::Message> recent(size_t limit = 50, RoomId room = LOBBY_ROOM_ID) const;

    // Replay recent messages to a callback (used when a new user joins).
    void replay(size_t limit, const std::function<void(const protocol::Message&)>& cb) const;

    size_t size() const;
    void   clear();

private:
    struct Entry {
        protocol::Message msg;
        std::string       sender_name;
        TimePoint         stored_at;
        RoomId            room_id = LOBBY_ROOM_ID;
    };

    Config cfg_;
    mutable std::mutex mu_;
    std::deque<Entry> ring_;

    void evict();
    void flush_to_disk(const Entry& entry);
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_MESSAGE_HISTORY_HPP
