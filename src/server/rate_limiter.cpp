#include <chatterbox/server/rate_limiter.hpp>
#include <chrono>

namespace chatterbox {
namespace server {

RateLimiter::RateLimiter(const Config& cfg) : cfg_(cfg) {}

void RateLimiter::evict_old(UserState& state, TimePoint now) const {
    auto cutoff = now - std::chrono::milliseconds(cfg_.window_ms);
    while (!state.timestamps.empty() && state.timestamps.front() < cutoff) {
        state.timestamps.pop_front();
    }
}

bool RateLimiter::check(UserId user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    auto now = Clock::now();
    auto& state = users_[user_id];

    // Still in penalty period
    if (state.penalty_until > now) {
        ++state.total_dropped;
        return false;
    }

    evict_old(state, now);

    if (state.timestamps.size() >= cfg_.max_messages) {
        if (!cfg_.soft_enforcement) {
            state.penalty_until = now + std::chrono::milliseconds(cfg_.penalty_ms);
            ++state.total_dropped;
            return false;
        }
    }

    state.timestamps.push_back(now);
    ++state.total_sent;
    return true;
}

void RateLimiter::remove_user(UserId user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    users_.erase(user_id);
}

void RateLimiter::reset() {
    std::lock_guard<std::mutex> lock(mu_);
    users_.clear();
}

RateLimiter::UserState RateLimiter::get_stats(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = users_.find(user_id);
    if (it == users_.end()) return {};
    return it->second;
}

uint64_t RateLimiter::total_drops() const {
    std::lock_guard<std::mutex> lock(mu_);
    uint64_t total = 0;
    for (const auto& kv : users_) {
        total += kv.second.total_dropped;
    }
    return total;
}

} // namespace server
} // namespace chatterbox
