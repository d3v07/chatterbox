#include <chatterbox/server/ban_registry.hpp>

namespace chatterbox {
namespace server {

bool BanEntry::is_permanent() const {
    return expires_at == TimePoint::max();
}

void BanRegistry::ban(UserId      user_id,
                      const std::string& username,
                      const std::string& reason,
                      UserId      banned_by,
                      uint32_t    duration_minutes) {
    std::lock_guard<std::mutex> lock(mu_);
    BanEntry e;
    e.user_id   = user_id;
    e.username  = username;
    e.reason    = reason;
    e.banned_by = banned_by;
    e.banned_at = Clock::now();
    e.expires_at = (duration_minutes == 0)
        ? TimePoint::max()
        : e.banned_at + std::chrono::minutes(duration_minutes);
    bans_[user_id] = std::move(e);
}

bool BanRegistry::pardon(UserId user_id) {
    std::lock_guard<std::mutex> lock(mu_);
    return bans_.erase(user_id) > 0;
}

bool BanRegistry::is_banned(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = bans_.find(user_id);
    if (it == bans_.end()) return false;
    if (it->second.expires_at != TimePoint::max() &&
        Clock::now() > it->second.expires_at) {
        return false; // expired
    }
    return true;
}

std::optional<BanEntry> BanRegistry::get(UserId user_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = bans_.find(user_id);
    if (it == bans_.end()) return std::nullopt;
    if (it->second.expires_at != TimePoint::max() &&
        Clock::now() > it->second.expires_at) {
        return std::nullopt;
    }
    return it->second;
}

size_t BanRegistry::prune_expired() {
    std::lock_guard<std::mutex> lock(mu_);
    auto now = Clock::now();
    size_t removed = 0;
    for (auto it = bans_.begin(); it != bans_.end(); ) {
        if (it->second.expires_at != TimePoint::max() && now > it->second.expires_at) {
            it = bans_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

size_t BanRegistry::size() const {
    std::lock_guard<std::mutex> lock(mu_);
    return bans_.size();
}

} // namespace server
} // namespace chatterbox
