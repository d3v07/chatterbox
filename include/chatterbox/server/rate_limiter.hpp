#ifndef CHATTERBOX_SERVER_RATE_LIMITER_HPP
#define CHATTERBOX_SERVER_RATE_LIMITER_HPP

#include <chatterbox/common.hpp>
#include <unordered_map>
#include <mutex>
#include <deque>
#include <chrono>

namespace chatterbox {
namespace server {

// Per-user sliding window rate limiter.
// Tracks message timestamps within a configurable window and rejects
// messages once the user exceeds the allowed burst count.
class RateLimiter {
public:
    struct Config {
        size_t max_messages      = 10;    // allowed messages per window
        int64_t window_ms        = 1000;  // sliding window length in ms
        int64_t penalty_ms       = 5000;  // cooldown after burst exceeded
        bool    soft_enforcement = false; // warn instead of drop
    };

    struct UserState {
        std::deque<TimePoint> timestamps;
        TimePoint penalty_until = TimePoint{};
        uint64_t  total_dropped = 0;
        uint64_t  total_sent    = 0;
    };

    explicit RateLimiter(const Config& cfg = Config());

    // Returns true when the message is allowed; false when it should be dropped.
    bool check(UserId user_id);

    // Remove tracking state for a disconnected user.
    void remove_user(UserId user_id);

    // Reset all state (e.g. on server restart).
    void reset();

    // Per-user statistics snapshot.
    UserState get_stats(UserId user_id) const;

    // Aggregate drop count across all users.
    uint64_t total_drops() const;

private:
    Config cfg_;
    mutable std::mutex mu_;
    std::unordered_map<UserId, UserState> users_;

    void evict_old(UserState& state, TimePoint now) const;
};

} // namespace server
} // namespace chatterbox

#endif // CHATTERBOX_SERVER_RATE_LIMITER_HPP
